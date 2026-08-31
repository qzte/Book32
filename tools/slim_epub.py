#!/usr/bin/env python3
"""Remove imagens, fontes e outros recursos não suportados de ficheiros EPUB.

A partição de ebooks do Book32 tem ~9,9 MB. Um EPUB comercial gasta a maior
parte do tamanho em capas, ilustrações e fontes embutidas — nenhuma das quais o
leitor usa: o EpubLoader só extrai texto e o render usa as fontes compiladas no
firmware (lib/Book32_Core/Fonts/). O mesmo vale para CSS, JavaScript e áudio/
vídeo embutidos: o TextRenderer não interpreta CSS (só atributos style= inline)
e o leitor não corre scripts nem reproduz media overlays. Retirar esse peso
costuma reduzir um livro a menos de um décimo do tamanho, sem perder uma linha
de texto.

Uso:
    python tools/slim_epub.py livro.epub                 # cria livro.slim.epub
    python tools/slim_epub.py livro.epub -o saida.epub
    python tools/slim_epub.py *.epub --in-place          # substitui o original
"""

import argparse
import os
import re
import shutil
import sys
import tempfile
import zipfile

# Extensões removidas. SVG entra aqui por ser imagem: o leitor ignora-o e há
# ficheiros SVG de capa com centenas de KB. CSS entra porque o TextRenderer
# nunca lê folhas de estilo (externas ou <style>), só style= inline.
STRIP_EXTENSIONS = {
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".tif", ".tiff", ".svg",
    ".ttf", ".otf", ".woff", ".woff2", ".eot",
    ".css",
    ".js", ".mjs",
    ".mp3", ".m4a", ".mp4", ".m4v", ".ogg", ".oga", ".wav", ".webm",
    ".smil",
}

# Tipos MIME correspondentes, para apanhar recursos com extensão invulgar.
STRIP_MIME_PREFIXES = ("image/", "font/", "audio/", "video/")
STRIP_MIME_EXACT = {
    "application/x-font-ttf",
    "application/x-font-truetype",
    "application/x-font-opentype",
    "application/vnd.ms-opentype",
    "application/font-woff",
    "application/font-sfnt",
    "text/css",
    "application/javascript",
    "text/javascript",
    "application/x-javascript",
    "application/smil+xml",
}

# META-INF/encryption.xml descreve a ofuscação de fontes (IDPF/Adobe): cada
# <enc:EncryptedData> tem um CipherReference cujo URI é o caminho dentro do
# ZIP (relativo à raiz do EPUB, não ao OPF). Depois de remover as fontes, essas
# entradas passam a referir ficheiros inexistentes.
ENCRYPTION_PATH = "META-INF/encryption.xml"


def is_strippable(name, media_type=""):
    if os.path.splitext(name)[1].lower() in STRIP_EXTENSIONS:
        return True
    media_type = media_type.lower()
    return media_type.startswith(STRIP_MIME_PREFIXES) or media_type in STRIP_MIME_EXACT


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


def clean_opf(xml, opf_path):
    """Retira do manifesto os itens que vamos remover do ZIP.

    Deixar entradas penduradas faria o leitor procurar ficheiros inexistentes,
    por isso o manifesto tem de acompanhar. Devolve (xml_novo, caminhos_zip).
    """
    removed_ids = set()
    removed_paths = set()

    def drop_item(match):
        tag = match.group(0)
        href = re.search(r'href\s*=\s*"([^"]*)"', tag)
        if not href:
            return tag
        media = re.search(r'media-type\s*=\s*"([^"]*)"', tag)
        if not is_strippable(href.group(1), media.group(1) if media else ""):
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
    # deixou de existir.
    def drop_meta(match):
        tag = match.group(0)
        content = re.search(r'content\s*=\s*"([^"]*)"', tag)
        if content and content.group(1) in removed_ids and 'name="cover"' in tag:
            return ""
        return tag

    xml = re.sub(r"<meta\b[^>]*/?>", drop_meta, xml)
    return xml, removed_paths


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


def slim(src, dst):
    """Escreve em dst uma cópia de src sem imagens, fontes, CSS/JS nem media.

    Devolve (bytes_originais, bytes_finais, n_removidos).
    """
    with zipfile.ZipFile(src) as zin:
        opf_paths = find_opf_paths(zin)
        rewritten = {}
        from_manifest = set()
        for opf in opf_paths:
            try:
                xml = zin.read(opf).decode("utf-8", "replace")
            except KeyError:
                continue
            new_xml, removed = clean_opf(xml, opf)
            rewritten[opf] = new_xml.encode("utf-8")
            from_manifest |= removed

        names = zin.namelist()
        # A extensão manda; o manifesto apanha o resto. Ficheiros dentro do ZIP
        # que nem sequer estão no manifesto (comuns em EPUBs gerados por
        # ferramentas) são apanhados pela extensão na mesma.
        to_remove = {n for n in names if is_strippable(n)} | from_manifest
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

    return os.path.getsize(src), os.path.getsize(dst), len(to_remove)


def human(n):
    for unit in ("B", "KB", "MB"):
        if n < 1024 or unit == "MB":
            return f"{n:,.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Remove imagens, fontes, CSS/JS e media embutidos de EPUBs para o Book32."
    )
    parser.add_argument("files", nargs="+", help="ficheiros .epub a processar")
    parser.add_argument("-o", "--output", help="ficheiro de saida (so com um input)")
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="substitui o original em vez de criar um .slim.epub",
    )
    args = parser.parse_args(argv)

    if args.output and len(args.files) > 1:
        parser.error("-o so pode ser usado com um ficheiro de entrada")
    if args.output and args.in_place:
        parser.error("-o e --in-place sao mutuamente exclusivos")

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
            before, after, removed = slim(src, tmp_path)
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
