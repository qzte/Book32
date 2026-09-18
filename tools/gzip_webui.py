"""Comprime a UI web antes de ela entrar na imagem do filesystem.

Porque: os ficheiros em data/ sao servidos tal e qual a partir do LittleFS, e
somam ~97 KB (script.js sozinho passa dos 59 KB). Isso e transferido por
inteiro a cada carregamento da pagina, e com o dispositivo em modo hotspot
nota-se bem. Comprimidos ficam a volta de um terco disso.

O ESPAsyncWebServer trata do resto sozinho: tanto o AsyncStaticWebHandler
(serveStatic) como o AsyncFileResponse (request->send(fs, path, type))
procuram "<caminho>.gz" quando o ficheiro simples nao existe e respondem com
Content-Encoding: gzip. O tipo de conteudo continua a sair do nome sem .gz,
por isso o script.js.gz e servido como JavaScript e nao como application/gzip.

data/ fica intocado: o que se comprime e uma copia dentro de $BUILD_DIR, e a
variavel PROJECT_DATA_DIR passa a apontar para la. Assim continua-se a editar
e a verificar os ficheiros normais (o CI corre `node --check data/script.js`),
e nunca ha duas versoes do mesmo ficheiro no repositorio a divergirem.

Reprodutibilidade: o cabecalho gzip leva um timestamp por omissao, o que faria
duas compilacoes do mesmo codigo produzirem littlefs.bin diferentes — e este
projecto assina o SHA-256 desse ficheiro (ver docs/plans/...-ota-ed25519-...).
Dai o mtime=0 abaixo.
"""

import gzip
import os
import shutil

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")  # noqa: F821  (injectado pelo PlatformIO)

# Extensoes que valem a pena: texto, e tudo o que esta em data/ hoje.
COMPRESS_EXT = (".html", ".css", ".js", ".json", ".svg", ".txt", ".xml")

# So os alvos que constroem a imagem do filesystem. Um `pio run` normal nao
# precisa disto e nao deve pagar por ele.
FS_TARGETS = ("buildfs", "uploadfs")


def _gzip_to(src_path, dst_path):
    """Comprime src para dst. Devolve True se valeu a pena (ficou menor)."""
    with open(src_path, "rb") as src:
        raw = src.read()
    with open(dst_path, "wb") as out:
        # mtime=0: ver a nota sobre reprodutibilidade no topo.
        with gzip.GzipFile(fileobj=out, mode="wb", compresslevel=9, mtime=0) as gz:
            gz.write(raw)
    return os.path.getsize(dst_path) < len(raw)


def build_compressed_data_dir():
    source = env.subst("$PROJECT_DATA_DIR")  # noqa: F821
    build_dir = env.subst("$BUILD_DIR")  # noqa: F821
    if not source or not os.path.isdir(source) or not build_dir:
        # Nunca deve acontecer, mas em vez de comprimir para o sitio errado
        # (ou de nao comprimir em silencio) deixa-se a imagem sair como
        # sempre saiu, com um aviso visivel no log da compilacao.
        print("AVISO: data/ ou BUILD_DIR nao resolvidos — UI web fica sem compressao")
        return None

    target = os.path.join(build_dir, "data_gz")
    if os.path.isdir(target):
        shutil.rmtree(target)
    os.makedirs(target)

    total_raw = 0
    total_out = 0
    for root, _dirs, files in os.walk(source):
        rel_root = os.path.relpath(root, source)
        out_root = target if rel_root == "." else os.path.join(target, rel_root)
        os.makedirs(out_root, exist_ok=True)

        for name in files:
            src_path = os.path.join(root, name)
            raw_size = os.path.getsize(src_path)
            total_raw += raw_size

            if not name.lower().endswith(COMPRESS_EXT):
                shutil.copy2(src_path, os.path.join(out_root, name))
                total_out += raw_size
                continue

            gz_path = os.path.join(out_root, name + ".gz")
            if _gzip_to(src_path, gz_path):
                total_out += os.path.getsize(gz_path)
            else:
                # Ja comprimido ou pequeno de mais para compensar: fica o
                # original, senao gastava-se espaco a "comprimir".
                os.remove(gz_path)
                shutil.copy2(src_path, os.path.join(out_root, name))
                total_out += raw_size

    print(
        "UI web comprimida: %d bytes -> %d bytes (%.0f%% do original)"
        % (total_raw, total_out, (100.0 * total_out / total_raw) if total_raw else 100.0)
    )
    return target


if any(t in COMMAND_LINE_TARGETS for t in FS_TARGETS):
    compressed = build_compressed_data_dir()
    if compressed:
        env.Replace(PROJECT_DATA_DIR=compressed)  # noqa: F821
