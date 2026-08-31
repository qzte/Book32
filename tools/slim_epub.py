#!/usr/bin/env python3
"""Remove imagens, fontes e outros recursos não suportados de ficheiros EPUB.

A partição de ebooks do Book32 tem ~9,9 MB. Um EPUB comercial gasta a maior
parte do tamanho em capas, ilustrações e fontes embutidas — a esmagadora
maioria das quais o leitor não usa: o EpubLoader só extrai texto (os
capítulos/spine nunca são tocados por este script) e o render usa as fontes
compiladas no firmware (lib/Book32_Core/Fonts/). O mesmo vale para CSS,
JavaScript e áudio/vídeo embutidos: o TextRenderer não interpreta CSS (só
atributos style= inline) e o leitor não corre scripts nem reproduz media
overlays. Retirar esse peso costuma reduzir um livro a menos de um décimo do
tamanho, sem perder uma linha de texto.

A única excepção real é a capa: o firmware descodifica JPEG e mostra a capa
verdadeira na biblioteca (EpubLoader::getCoverImageData + CoverImage.cpp), por
isso --keep-cover vale a pena por omissão para quem quiser essa miniatura —
o custo é só o tamanho da própria capa, não de todas as ilustrações do livro.
O descodificador do firmware só sabe ler JPEG: dar-lhe outro formato arrisca
crash no dispositivo em vez de simplesmente não mostrar capa. Por isso, com
--keep-cover, uma capa que não seja JPEG (PNG/GIF/WebP são comuns em EPUB) é
convertida para JPEG com Pillow (`pip install pillow`), se estiver instalado;
sem Pillow, ou se a conversão falhar por algum motivo, a capa é removida como
antes — nunca é escrito no EPUB um formato que o firmware não saiba abrir.

Uso:
    python tools/slim_epub.py livro.epub                 # cria livro.slim.epub
    python tools/slim_epub.py livro.epub -o saida.epub
    python tools/slim_epub.py *.epub --in-place          # substitui o original
    python tools/slim_epub.py livro.epub --keep-cover     # mantém a capa (converte para JPEG se preciso)
    python tools/slim_epub.py livro.epub --keep-images --keep-fonts
"""

import argparse
import io
import os
import re
import shutil
import sys
import tempfile
import zipfile

# Recursos agrupados por categoria, para permitir escolher o que manter
# (--keep-cover/--keep-images/--keep-fonts/--keep-css/--keep-js/--keep-media).
# SVG entra em "images" por ser imagem: o leitor ignora-o e há ficheiros SVG
# de capa com centenas de KB. CSS entra porque o TextRenderer nunca lê
# folhas de estilo (externas ou <style>), só style= inline.
CATEGORIES = {
    "images": {
        "ext": {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".tif", ".tiff", ".svg"},
        "mime_prefixes": ("image/",),
        "mime_exact": set(),
    },
    "fonts": {
        "ext": {".ttf", ".otf", ".woff", ".woff2", ".eot"},
        "mime_prefixes": ("font/",),
        "mime_exact": {
            "application/x-font-ttf",
            "application/x-font-truetype",
            "application/x-font-opentype",
            "application/vnd.ms-opentype",
            "application/font-woff",
            "application/font-sfnt",
        },
    },
    "css": {
        "ext": {".css"},
        "mime_prefixes": (),
        "mime_exact": {"text/css"},
    },
    "js": {
        "ext": {".js", ".mjs"},
        "mime_prefixes": (),
        "mime_exact": {"application/javascript", "text/javascript", "application/x-javascript"},
    },
    "media": {
        "ext": {".mp3", ".m4a", ".mp4", ".m4v", ".ogg", ".oga", ".wav", ".webm", ".smil"},
        "mime_prefixes": ("audio/", "video/"),
        "mime_exact": {"application/smil+xml"},
    },
}

# META-INF/encryption.xml descreve a ofuscação de fontes (IDPF/Adobe): cada
# <enc:EncryptedData> tem um CipherReference cujo URI é o caminho dentro do
# ZIP (relativo à raiz do EPUB, não ao OPF). Depois de remover as fontes, essas
# entradas passam a referir ficheiros inexistentes.
ENCRYPTION_PATH = "META-INF/encryption.xml"


def classify(name, media_type=""):
    """Devolve a categoria do recurso (ver CATEGORIES), ou None se não for
    um dos tipos que este script sabe remover (ex.: XHTML, OPF, NCX)."""
    ext = os.path.splitext(name)[1].lower()
    media_type = media_type.lower()
    for cat, spec in CATEGORIES.items():
        if ext in spec["ext"]:
            return cat
        if media_type and (media_type in spec["mime_exact"] or media_type.startswith(spec["mime_prefixes"])):
            return cat
    return None


def find_opf_paths(zf):
    """Caminhos dos ficheiros OPF, via META-INF/container.xml.

    Se o container faltar ou for ilegível, recorre a procurar qualquer .opf: um
    EPUB inválido continua a ser aparado, só sem a certeza de qual é a raiz.
    """
    try:
        container = zf.read("META-INF/container.xml").decode("utf-8", "replace")
        paths = re.findall(r'full-path\s*=\s*"([^"]+)"', container)
        if paths:
            return paths
    except KeyError:
        pass
    return [n for n in zf.namelist() if n.lower().endswith(".opf")]


def resolve(opf_path, href):
    """Resolve um href do manifesto para o caminho dentro do ZIP."""
    href = href.split("#", 1)[0].split("?", 1)[0]
    base = os.path.dirname(opf_path)
    joined = os.path.join(base, href) if base else href
    return os.path.normpath(joined).replace("\\", "/")


def find_cover_href(xml, items):
    """Href (tal como está no manifesto) do item de capa, ou None.

    Mesma prioridade que o firmware usa (EpubLoader::parseOpf): primeiro o
    <item properties="cover-image"> do EPUB3, senão o <meta name="cover"
    content="ID"> do EPUB2 resolvido contra o manifesto.
    """
    for item_id, href, media, properties in items:
        if properties and "cover-image" in properties.split():
            return href
    meta = re.search(r'<meta\b[^>]*\bname\s*=\s*"cover"[^>]*/?>', xml)
    if meta:
        content = re.search(r'content\s*=\s*"([^"]*)"', meta.group(0))
        if content:
            for item_id, href, media, properties in items:
                if item_id == content.group(1):
                    return href
    return None


def convert_cover_to_jpeg(data):
    """Converte bytes de imagem (tipicamente PNG) para JPEG baseline, para a
    capa poder ficar no EPUB mesmo não tendo nascido em JPEG — CoverImage.cpp
    só descodifica esse formato. Nunca levanta excepção: devolve None se o
    Pillow não estiver instalado, os bytes não forem uma imagem reconhecível,
    ou a conversão falhar por qualquer razão (o chamador cai então para o
    comportamento seguro de sempre, remover a capa).
    """
    try:
        from PIL import Image
    except ImportError:
        return None
    try:
        img = Image.open(io.BytesIO(data))
        img.load()
        if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
            # Achata a transparência sobre fundo branco em vez de a deixar o
            # Pillow descartar sem mais (ficaria preto por omissão) — o ecrã
            # e-ink é branco, por isso um fundo transparente deve ficar branco.
            img = img.convert("RGBA")
            background = Image.new("RGB", img.size, (255, 255, 255))
            background.paste(img, mask=img.split()[-1])
            img = background
        else:
            img = img.convert("RGB")
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=85)  # baseline por omissao: progressivo e mais arriscado num descodificador embutido
        return buf.getvalue()
    except Exception:
        return None


def parse_items(xml):
    """Lista (id, href, media-type, properties) de cada <item> do manifesto."""
    items = []
    for match in re.finditer(r"<item\b[^>]*/?>", xml):
        tag = match.group(0)
        href = re.search(r'href\s*=\s*"([^"]*)"', tag)
        if not href:
            continue
        item_id = re.search(r'\bid\s*=\s*"([^"]*)"', tag)
        media = re.search(r'media-type\s*=\s*"([^"]*)"', tag)
        properties = re.search(r'\bproperties\s*=\s*"([^"]*)"', tag)
        items.append((
            item_id.group(1) if item_id else "",
            href.group(1),
            media.group(1) if media else "",
            properties.group(1) if properties else "",
        ))
    return items


def clean_opf(xml, opf_path, keep_categories, keep_cover, convert_cover=None, existing_paths=frozenset()):
    """Retira do manifesto os itens que vamos remover do ZIP.

    Deixar entradas penduradas faria o leitor procurar ficheiros inexistentes,
    por isso o manifesto tem de acompanhar. convert_cover, se dado, é chamado
    com o caminho ZIP de uma capa não-JPEG e devolve bytes JPEG (ou None se
    não conseguir); existing_paths evita que o novo nome ".jpg" colida com um
    ficheiro que já exista no ZIP. Devolve (xml_novo, caminhos_zip_removidos,
    caminho_zip_da_capa_mantida_ou_None, {caminho_zip_novo: bytes_jpeg}).
    """
    items = parse_items(xml)
    cover_href = find_cover_href(xml, items)

    removed_ids = set()
    removed_paths = set()
    kept_cover_path = [None]  # células mutáveis só para o closure poder escrever
    extra_files = {}

    def is_jpeg(href, media):
        ext = os.path.splitext(href)[1].lower()
        return ext in (".jpg", ".jpeg") or media.lower() == "image/jpeg"

    def drop_item(match):
        tag = match.group(0)
        href = re.search(r'href\s*=\s*"([^"]*)"', tag)
        if not href:
            return tag
        media = re.search(r'media-type\s*=\s*"([^"]*)"', tag)
        media_val = media.group(1) if media else ""
        cat = classify(href.group(1), media_val)
        if cat is None or cat in keep_categories:
            return tag
        if cat == "images" and keep_cover and href.group(1) == cover_href:
            if is_jpeg(href.group(1), media_val):
                kept_cover_path[0] = resolve(opf_path, href.group(1))
                return tag
            # Não é JPEG: CoverImage.cpp só sabe descodificar esse formato,
            # por isso só fica se convert_cover a conseguir converter — senão
            # cai para o strip normal abaixo, tal como sempre foi.
            if convert_cover is not None:
                jpeg_bytes = convert_cover(resolve(opf_path, href.group(1)))
                if jpeg_bytes is not None:
                    dst_href = os.path.splitext(href.group(1))[0] + ".jpg"
                    dst_path = resolve(opf_path, dst_href)
                    if dst_path not in existing_paths and dst_path not in extra_files:
                        extra_files[dst_path] = jpeg_bytes
                        kept_cover_path[0] = dst_path
                        removed_paths.add(resolve(opf_path, href.group(1)))  # o original é substituído, não mantido
                        new_tag = re.sub(r'href\s*=\s*"[^"]*"', 'href="%s"' % dst_href, tag, count=1)
                        if media:
                            new_tag = re.sub(r'media-type\s*=\s*"[^"]*"', 'media-type="image/jpeg"', new_tag, count=1)
                        return new_tag
        item_id = re.search(r'\bid\s*=\s*"([^"]*)"', tag)
        if item_id:
            removed_ids.add(item_id.group(1))
        removed_paths.add(resolve(opf_path, href.group(1)))
        return ""

    xml = re.sub(r"<item\b[^>]*/?>", drop_item, xml)

    # O spine não deve referir recursos destes, mas um EPUB mal formado pode
    # fazê-lo — e aí o leitor abriria uma "página" que já não existe.
    def drop_itemref(match):
        idref = re.search(r'idref\s*=\s*"([^"]*)"', match.group(0))
        return "" if idref and idref.group(1) in removed_ids else match.group(0)

    xml = re.sub(r"<itemref\b[^>]*/?>", drop_itemref, xml)

    # <meta name="cover" content="id-da-capa"/> aponta para uma imagem que
    # deixou de existir (a não ser que a capa tenha sido mantida).
    def drop_meta(match):
        tag = match.group(0)
        content = re.search(r'content\s*=\s*"([^"]*)"', tag)
        if content and content.group(1) in removed_ids and 'name="cover"' in tag:
            return ""
        return tag

    xml = re.sub(r"<meta\b[^>]*/?>", drop_meta, xml)
    return xml, removed_paths, kept_cover_path[0], extra_files


def clean_encryption(xml, removed_paths):
    """Retira do encryption.xml as entradas dos ficheiros removidos.

    Devolve o XML reescrito, ou None se não sobrar nenhuma
    <enc:EncryptedData> — nesse caso o chamador remove o ficheiro todo.
    """
    def drop_entry(match):
        block = match.group(0)
        uri = re.search(r'URI\s*=\s*"([^"]+)"', block)
        if uri and uri.group(1).lstrip("/") in removed_paths:
            return ""
        return block

    new_xml = re.sub(
        r"<enc:EncryptedData\b.*?</enc:EncryptedData>",
        drop_entry,
        xml,
        flags=re.DOTALL,
    )
    if not re.search(r"<enc:EncryptedData\b", new_xml):
        return None
    return new_xml


def slim(src, dst, keep_categories=frozenset(), keep_cover=False):
    """Escreve em dst uma cópia de src sem os recursos não escolhidos para manter.

    keep_categories é um subconjunto de {"images", "fonts", "css", "js",
    "media"}; keep_cover mantém a capa mesmo com "images" fora desse
    conjunto. Devolve (bytes_originais, bytes_finais, n_removidos).
    """
    with zipfile.ZipFile(src) as zin:
        names = zin.namelist()
        existing_paths = set(names)

        def try_convert_cover(zip_path):
            try:
                data = zin.read(zip_path)
            except KeyError:
                return None
            return convert_cover_to_jpeg(data)

        convert_cover = try_convert_cover if keep_cover else None

        opf_paths = find_opf_paths(zin)
        rewritten = {}
        from_manifest = set()
        cover_paths = set()
        extra_files = {}
        for opf in opf_paths:
            try:
                xml = zin.read(opf).decode("utf-8", "replace")
            except KeyError:
                continue
            new_xml, removed, cover_path, extras = clean_opf(
                xml, opf, keep_categories, keep_cover, convert_cover, existing_paths)
            rewritten[opf] = new_xml.encode("utf-8")
            from_manifest |= removed
            extra_files.update(extras)
            if cover_path:
                cover_paths.add(cover_path)

        # A extensão manda; o manifesto apanha o resto. Ficheiros dentro do ZIP
        # que nem sequer estão no manifesto (comuns em EPUBs gerados por
        # ferramentas) são apanhados pela extensão na mesma.
        stripped_cats = {n: classify(n) for n in names}
        to_remove = {n for n, cat in stripped_cats.items() if cat and cat not in keep_categories}
        to_remove |= from_manifest
        if keep_cover:
            to_remove -= cover_paths
        to_remove &= set(names)

        if ENCRYPTION_PATH in names:
            enc_xml = zin.read(ENCRYPTION_PATH).decode("utf-8", "replace")
            new_enc = clean_encryption(enc_xml, to_remove)
            if new_enc is None:
                to_remove.add(ENCRYPTION_PATH)
            elif new_enc != enc_xml:
                rewritten[ENCRYPTION_PATH] = new_enc.encode("utf-8")

        with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zout:
            # "mimetype" tem de ser a primeira entrada e não comprimida, senão
            # alguns leitores recusam o ficheiro.
            if "mimetype" in names:
                zout.writestr(
                    zipfile.ZipInfo("mimetype"),
                    zin.read("mimetype"),
                    zipfile.ZIP_STORED,
                )
            for info in zin.infolist():
                if info.filename == "mimetype" or info.filename in to_remove:
                    continue
                if info.is_dir():
                    continue
                data = rewritten.get(info.filename) or zin.read(info.filename)
                zout.writestr(info.filename, data, zipfile.ZIP_DEFLATED)
            for path, data in extra_files.items():
                zout.writestr(path, data, zipfile.ZIP_DEFLATED)

    return os.path.getsize(src), os.path.getsize(dst), len(to_remove)


def human(n):
    for unit in ("B", "KB", "MB"):
        if n < 1024 or unit == "MB":
            return f"{n:,.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Remove imagens, fontes, CSS/JS e media embutidos de EPUBs para o Book32.",
        epilog="Os capítulos (o XHTML da spine) nunca são tocados por este script, "
               "independentemente das opções --keep-*.",
    )
    parser.add_argument("files", nargs="+", help="ficheiros .epub a processar")
    parser.add_argument("-o", "--output", help="ficheiro de saida (so com um input)")
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="substitui o original em vez de criar um .slim.epub",
    )
    parser.add_argument(
        "--keep-cover",
        action="store_true",
        help="mantem a capa do livro; converte para JPEG com Pillow se nao ja for (o firmware so descodifica JPEG)",
    )
    parser.add_argument(
        "--keep-images",
        action="store_true",
        help="mantem todas as imagens, nao so a capa (o leitor nao as mostra dentro do texto)",
    )
    parser.add_argument("--keep-fonts", action="store_true", help="mantem as fontes embutidas (o leitor usa sempre as fontes do firmware)")
    parser.add_argument("--keep-css", action="store_true", help="mantem CSS embutido (o leitor nao o interpreta)")
    parser.add_argument("--keep-js", action="store_true", help="mantem JavaScript embutido (o leitor nao o executa)")
    parser.add_argument("--keep-media", action="store_true", help="mantem audio/video/SMIL embutidos (o leitor nao os reproduz)")
    args = parser.parse_args(argv)

    if args.output and len(args.files) > 1:
        parser.error("-o so pode ser usado com um ficheiro de entrada")
    if args.output and args.in_place:
        parser.error("-o e --in-place sao mutuamente exclusivos")

    keep_categories = set()
    if args.keep_images:
        keep_categories.add("images")
    if args.keep_fonts:
        keep_categories.add("fonts")
    if args.keep_css:
        keep_categories.add("css")
    if args.keep_js:
        keep_categories.add("js")
    if args.keep_media:
        keep_categories.add("media")

    total_before = total_after = 0
    failures = 0

    for src in args.files:
        if not os.path.isfile(src):
            print(f"ERRO: {src}: nao encontrado", file=sys.stderr)
            failures += 1
            continue
        if not zipfile.is_zipfile(src):
            print(f"ERRO: {src}: nao e um EPUB valido (nao e ZIP)", file=sys.stderr)
            failures += 1
            continue

        if args.output:
            dst = args.output
        elif args.in_place:
            dst = src
        else:
            base, ext = os.path.splitext(src)
            dst = base + ".slim" + ext

        # Escrever sempre para um temporário: --in-place não pode truncar o
        # original antes de a nova versão estar completa, e um erro a meio não
        # deve deixar um EPUB meio escrito no lugar de um bom.
        tmp_fd, tmp_path = tempfile.mkstemp(suffix=".epub", dir=os.path.dirname(os.path.abspath(dst)))
        os.close(tmp_fd)
        try:
            before, after, removed = slim(src, tmp_path, keep_categories, args.keep_cover)
            shutil.move(tmp_path, dst)
        except Exception as exc:  # noqa: BLE001 - qualquer falha é do ficheiro
            if os.path.exists(tmp_path):
                os.remove(tmp_path)
            print(f"ERRO: {src}: {exc}", file=sys.stderr)
            failures += 1
            continue

        total_before += before
        total_after += after
        pct = (1 - after / before) * 100 if before else 0
        print(f"{os.path.basename(src)}: {human(before)} -> {human(after)} "
              f"(-{pct:.0f}%, {removed} recursos removidos) => {dst}")

    if total_before and len(args.files) > 1:
        pct = (1 - total_after / total_before) * 100
        print(f"\nTotal: {human(total_before)} -> {human(total_after)} (-{pct:.0f}%)")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
