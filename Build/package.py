"""Assemble installable VST3 bundles and the corresponding source archive."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent.parent
VERSION = '0.1.4'
JUCE_COMMIT = '4f43011b96eb0636104cb3e433894cda98243626'
PLUGINS = [('Exp3r1ment4l', 'Exp3r1ment4l'),
           ('Exp3r1ment4lInstrument', 'Exp3r1ment4l Synth')]


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def add_file(archive, path, arcname):
    info = zipfile.ZipInfo.from_file(path, arcname.as_posix())
    info.compress_type = zipfile.ZIP_DEFLATED
    with path.open('rb') as stream:
        archive.writestr(info, stream.read())


def add_tree(archive, base, prefix, skip_git=False):
    for path in sorted(base.rglob('*')):
        if skip_git and '.git' in path.relative_to(base).parts:
            continue
        if path.is_symlink():
            info = zipfile.ZipInfo((prefix / path.relative_to(base)).as_posix())
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | path.lstat().st_mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, os.readlink(path).encode())
        elif path.is_file():
            add_file(archive, path, prefix / path.relative_to(base))


def package_source(output, juce):
    if not juce.is_dir() or not (juce / 'modules').is_dir():
        raise FileNotFoundError(f'JUCE source tree not found: {juce}')
    archive_path = output / f'Exp3r1ment4l-{VERSION}-source.zip'
    included = ['.clang-format', 'CMakeLists.txt', 'LICENSE.txt', 'README.md',
                'README.ru.md', 'THIRD_PARTY.md', 'Source', 'Tests', 'docs', 'Build']
    with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for item in included:
            path = ROOT / item
            if path.is_dir():
                add_tree(archive, path, Path('Exp3r1ment4l') / item)
            elif path.is_file():
                add_file(archive, path, Path('Exp3r1ment4l') / item)
        (output / 'SOURCE_DISTRIBUTION.txt').write_text(
            f'Exp3r1ment4l {VERSION} source distribution.\n'
            f'JUCE 7.0.12 source is included at JUCE/ (commit {JUCE_COMMIT}).\n'
            'The macOS SDK compatibility patch is in Exp3r1ment4l/Build/patch_juce_macos.py.\n'
            'Project and dependency license notices are included in the archive.\n', encoding='utf-8')
        add_file(archive, output / 'SOURCE_DISTRIBUTION.txt', Path('Exp3r1ment4l/SOURCE_DISTRIBUTION.txt'))
        add_tree(archive, juce, Path('JUCE'), skip_git=True)
    write_checksum(archive_path)


def write_checksum(path):
    (path.parent / f'{path.name}.sha256').write_text(f'{sha256(path)}  {path.name}\n', encoding='ascii')


def remove_stage(stage, output, expected_name):
    output = output.resolve()
    resolved = stage.resolve()
    if stage.name != expected_name or resolved.name != expected_name or resolved.parent != output:
        raise RuntimeError(f'Refusing to remove unexpected package staging path: {stage}')
    if stage.is_symlink():
        raise RuntimeError(f'Refusing to remove a symlink used as package staging: {stage}')
    if stage.exists():
        shutil.rmtree(stage)


def package_binaries(platform, build, output):
    if platform not in ('win', 'mac'):
        raise ValueError(f'Unsupported binary platform: {platform}')
    if platform == 'mac' and shutil.which('codesign') is None:
        raise RuntimeError('macOS packaging requires codesign.')
    output.mkdir(parents=True, exist_ok=True)
    stage_name = f'Exp3r1ment4l-{VERSION}-{platform}-vst3'
    stage = output / stage_name
    remove_stage(stage, output, stage_name)
    stage.mkdir()
    artefacts = build / 'Exp3r1ment4l_artefacts/Release/VST3'
    instrument_artefacts = build / 'Exp3r1ment4lInstrument_artefacts/Release/VST3'
    docs = ['README.md', 'README.ru.md', 'LICENSE.txt', 'THIRD_PARTY.md']
    for item in docs:
        shutil.copy2(ROOT / item, stage / item)
    (stage / 'INSTALL.txt').write_text(
        'Exp3r1ment4l 0.1.4 — installation / установка\n\n'
        'EN: See README.md for installation details.\n'
        'RU: Инструкции по установке см. в README.ru.md.\n', encoding='utf-8')
    for target, product in PLUGINS:
        source = (artefacts if target == 'Exp3r1ment4l' else instrument_artefacts) / f'{product}.vst3'
        if not source.is_dir():
            raise FileNotFoundError(f'Built VST3 bundle not found: {source}')
        destination = stage / f'{product}.vst3'
        shutil.copytree(source, destination, symlinks=True)
        if platform == 'mac':
            subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(destination)], check=True)
    archive_path = output / f'Exp3r1ment4l-{VERSION}-{platform}-vst3.zip'
    if platform == 'mac':
        with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            add_tree(archive, stage, Path())
    else:
        with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            add_tree(archive, stage, Path())
    remove_stage(stage, output, stage_name)
    write_checksum(archive_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--platform', required=True, choices=['win', 'mac', 'source'])
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--juce-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.platform == 'source':
        package_source(output, args.juce_dir.resolve())
    else:
        if args.build_dir is None:
            parser.error('--build-dir is required for binary packages')
        package_binaries(args.platform, args.build_dir.resolve(), output)


if __name__ == '__main__':
    main()
