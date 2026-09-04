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

A única excepção real é a capa: o firmware mostra a capa verdadeira na
biblioteca e, desde a v1.21.0, também como página de capa a ocupar o ecrã
(EpubLoader::getCoverImageData + CoverImage.cpp), por isso --keep-cover vale a
pena por omissão — o custo é só o tamanho da própria capa, não de todas as
ilustrações do livro.

**JPEG e PNG ficam como estão**: são os dois formatos que o firmware
descodifica (CoverImage.cpp decide pelos bytes do ficheiro, não pela
extensão). Até à v1.21.0 só havia JPEG, e por isso este script convertia — ou,
sem Pillow instalado, apagava — qualquer capa PNG. Deixou de o fazer: apagar a
capa era a pior das saídas possíveis, e a conversão já não é precisa.

Só os formatos que o firmware mesmo não abre (GIF, WebP, SVG, TIFF...) é que
são convertidos para JPEG com Pillow (`pip install pillow`). O mesmo vale para
um PNG que esteja fora do que o descodificador do dispositivo aguenta
(entrelaçado, 16 bits por canal, mais de ~6 MP ou mais de 4000 px de largura —
ver kMaxPngPixels e pngLineFits em CoverImage.cpp). Sem Pillow instalado, ou
se a conversão falhar, a capa fica no EPUB à mesma, com um aviso: ocupa espaço
sem aparecer no dispositivo, mas fica lá para quando isso mudar — e nada no
firmware trava por causa de um formato que não saiba ler, só não mostra capa.

Uso:
    python tools/slim_epub.py livro.epub                 # cria livro.slim.epub
    python tools/slim_epub.py livro.epub -o saida.epub
    python tools/slim_epub.py *.epub --in-place          # substitui o original
    python tools/slim_epub.py livro.epub --keep-cover     # mantém a capa (JPEG/PNG ficam como estão)
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


# Limites do descodificador de PNG do firmware (ver kMaxPngPixels e
# pngLineFits em lib/Apps/AppReader/CoverImage.cpp, e o que a própria PNGdec
# recusa). Um PNG fora destes limites é guardado na mesma, mas o dispositivo
# não o mostra — daí valer a pena convertê-lo quando há Pillow.
PNG_MAX_PIXELS = 6_000_000
PNG_MAX_WIDTH = 4000


def png_needs_conversion(data):
    """True se estes bytes forem um PNG que o firmware não consegue mostrar.

    Lê só o cabeçalho IHDR, sem Pillow: assinatura (8 bytes), depois
    comprimento e tipo do chunk (8), largura e altura (4+4), profundidade de
    bit, tipo de cor, compressão, filtro e entrelaçamento. Um ficheiro
    truncado ou que não seja PNG devolve False — quem chama trata-o pelo tipo
    que o manifesto declara, não por adivinhação daqui.
    """
    if len(data) < 29 or not data.startswith(b"\x89PNG\r\n\x1a\n"):
        return False
    if data[12:16] != b"IHDR":
        return False
    width = int.from_bytes(data[16:20], "big")
    height = int.from_bytes(data[20:24], "big")
    bit_depth = data[24]
    interlace = data[28]
    if width <= 0 or height <= 0:
        return False
    return (interlace != 0 or bit_depth > 8 or width > PNG_MAX_WIDTH
            or width * height > PNG_MAX_PIXELS)


def convert_cover_to_jpeg(data):
    """Converte bytes de imagem para JPEG baseline, para a capa poder ficar no
    EPUB num formato que o dispositivo mostre. Nunca levanta excepção: devolve
    None se o Pillow não estiver instalado, os bytes não forem uma imagem
    reconhecível, ou a conversão falhar por qualquer razão — e nesse caso o
    chamador mantém a capa original como está, em vez de a apagar.
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


def clean_opf(xml, opf_path, keep_categories, keep_cover, convert_cover=None, existing_paths=frozenset(),
              cover_needs_conversion=None, unconvertible_covers=None):
    """Retira do manifesto os itens que vamos remover do ZIP.

    Deixar entradas penduradas faria o leitor procurar ficheiros inexistentes,
    por isso o manifesto tem de acompanhar. convert_cover, se dado, é chamado
    com o caminho ZIP de uma capa não-JPEG e devolve bytes JPEG (ou None se
    não conseguir); existing_paths evita que o novo nome ".jpg" colida com um
    ficheiro que já exista no ZIP. Devolve (xml_novo, caminhos_zip_removidos,
    caminho_zip_da_capa_mantida_ou_None, {caminho_zip_novo: bytes_jpeg}).

    cover_needs_conversion, se dado, recebe o caminho ZIP de uma capa PNG e
    diz se o firmware não a conseguiria mostrar; unconvertible_covers, se
    dado, recebe (href, era_png_fora_dos_limites) de cada capa que ficou num
    formato que o dispositivo não mostra, para o chamador poder avisar.
    """
    items = parse_items(xml)
    cover_href = find_cover_href(xml, items)
    if unconvertible_covers is None:
        unconvertible_covers = []

    removed_ids = set()
    removed_paths = set()
    kept_cover_path = [None]  # células mutáveis só para o closure poder escrever
    extra_files = {}

    def is_jpeg(href, media):
        ext = os.path.splitext(href)[1].lower()
        return ext in (".jpg", ".jpeg") or media.lower() == "image/jpeg"

    def is_png(href, media):
        ext = os.path.splitext(href)[1].lower()
        return ext == ".png" or media.lower() == "image/png"

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
            cover_zip_path = resolve(opf_path, href.group(1))
            # JPEG fica sempre; PNG fica desde que o descodificador do
            # dispositivo o consiga abrir (ver png_needs_conversion).
            if is_jpeg(href.group(1), media_val):
                kept_cover_path[0] = cover_zip_path
                return tag
            oversized_png = False
            if is_png(href.group(1), media_val):
                oversized_png = cover_needs_conversion is not None and cover_needs_conversion(cover_zip_path)
                if not oversized_png:
                    kept_cover_path[0] = cover_zip_path
                    return tag
            # Formato que o firmware não abre (GIF/WebP/SVG/...), ou um PNG
            # fora dos limites dele: converte-se para JPEG se houver Pillow.
            if convert_cover is not None:
                jpeg_bytes = convert_cover(cover_zip_path)
                if jpeg_bytes is not None:
                    dst_href = os.path.splitext(href.group(1))[0] + ".jpg"
                    dst_path = resolve(opf_path, dst_href)
                    if dst_path not in existing_paths and dst_path not in extra_files:
                        extra_files[dst_path] = jpeg_bytes
                        kept_cover_path[0] = dst_path
                        removed_paths.add(cover_zip_path)  # o original é substituído, não mantido
                        new_tag = re.sub(r'href\s*=\s*"[^"]*"', 'href="%s"' % dst_href, tag, count=1)
                        if media:
                            new_tag = re.sub(r'media-type\s*=\s*"[^"]*"', 'media-type="image/jpeg"', new_tag, count=1)
                        return new_tag
            # Sem Pillow (ou conversão falhada): a capa fica como está. Ocupa
            # espaço sem aparecer no dispositivo, mas apagá-la — que era o que
            # este script fazia — é pior: o livro perde a capa para sempre, e
            # o firmware nunca trava por um formato que não saiba ler.
            unconvertible_covers.append((href.group(1), oversized_png))
            kept_cover_path[0] = cover_zip_path
            return tag
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
    conjunto. Devolve (bytes_originais, bytes_finais, n_removidos,
    capas_que_o_dispositivo_nao_mostra).
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

        def cover_png_needs_conversion(zip_path):
            try:
                data = zin.read(zip_path)
            except KeyError:
                return False
            return png_needs_conversion(data)

        convert_cover = try_convert_cover if keep_cover else None
        cover_needs_conversion = cover_png_needs_conversion if keep_cover else None
        unconvertible_covers = []

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
                xml, opf, keep_categories, keep_cover, convert_cover, existing_paths,
                cover_needs_conversion, unconvertible_covers)
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

    return os.path.getsize(src), os.path.getsize(dst), len(to_remove), unconvertible_covers


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
        help="mantem a capa do livro; JPEG e PNG ficam como estao, outros formatos sao convertidos para JPEG com Pillow",
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
            before, after, removed, unconvertible = slim(src, tmp_path, keep_categories,
                                                         args.keep_cover)
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
        for href, oversized_png in unconvertible:
            motivo = ("PNG fora dos limites do descodificador do dispositivo"
                      if oversized_png else "formato que o dispositivo nao le")
            print(f"  aviso: capa mantida mas nao vai aparecer no Book32 ({motivo}): {href}\n"
                  f"         'pip install pillow' e correr outra vez converte-a para JPEG.",
                  file=sys.stderr)

    if total_before and len(args.files) > 1:
        pct = (1 - total_after / total_before) * 100
        print(f"\nTotal: {human(total_before)} -> {human(total_after)} (-{pct:.0f}%)")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
