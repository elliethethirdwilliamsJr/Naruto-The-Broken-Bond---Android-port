"""Build the isolated Android diagnostic APK with installed SDK tools, no Gradle.

Native probes are embedded as extracted APK libraries and executed from the
package's nativeLibraryDir. Original game assets are never bundled in the APK.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, default=Path(str(Path.home() / 'Android')))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
sdk = args.sdk.resolve()
build_tools = sdk / 'build-tools/35.0.0'
android = sdk / 'platforms/android-35/android.jar'
ndk = sdk / 'ndk/28.2.13676358'
llvm = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin'
java = Path(shutil.which('java')).resolve().parent
out = root / 'build/android-apk'
out.mkdir(parents=True, exist_ok=True)
stage = out / 'stage'
if stage.exists():
    assert stage.resolve().parent == out.resolve()
    shutil.rmtree(stage)
classes = stage / 'classes'; classes.mkdir(parents=True)
dex = stage / 'dex'; dex.mkdir()
assets = stage / 'assets/diagnostics'; assets.mkdir(parents=True)
libs = stage / 'lib/arm64-v8a'; libs.mkdir(parents=True)
source = root / 'tools/android-apk'

def run(command):
    subprocess.run([str(x) for x in command], cwd=root, check=True)

native = {
    'libbb_guest_probe.so': root / 'build/android-runtime/android_guest_probe',
    'libbb_texture_probe.so': root / 'build/android-runtime/android_texture_probe',
    'libbb_clip_probe.so': root / 'build/android-runtime/android_clip_probe',
    'libbb_descriptor_probe.so': root / 'build/android-runtime/android_descriptor_probe',
    'libbb_renderer_probe.so': root / 'build/android-runtime/android_renderer_probe',
    'libbroken_bond_renderer_jni.so': root / 'build/android-runtime/libbroken_bond_renderer_jni.so',
    'libbroken_bond_game.so': root / 'build/android-runtime/libbroken_bond_game.so',
    'libbroken_bond_guest.so': root / 'build/android-runtime/libbroken_bond_guest.so',
    'librexruntime.so': root / 'sdk/out/android-arm64/librexruntime.so',
    'libc++_shared.so': ndk / 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so',
}
probes = {'android_guest_probe': ['runtime_probe.cpp', 'guest_checks.cpp'],
          'android_texture_probe': ['texture_probe.cpp'], 'android_clip_probe': ['clip_probe.cpp'],
          'android_descriptor_probe': ['descriptor_probe.cpp'],
          'android_renderer_probe': ['renderer_probe.cpp'],
          'broken_bond_renderer_jni': ['renderer_surface.cpp'],
          'broken_bond_game': ['game_main.cpp', 'touch_input.cpp']}
for target, names in probes.items():
    for name in names:
        cpp = root / 'tools/android-runtime' / name
        obj = root / f'build/android-runtime/CMakeFiles/{target}.dir/{name}.o'
        if cpp.stat().st_mtime_ns > obj.stat().st_mtime_ns:
            raise SystemExit(f'Stale native diagnostic object: {obj}. Rebuild before packaging.')
descriptor_object = root / 'build/android-runtime/CMakeFiles/android_descriptor_probe.dir/descriptor_probe.cpp.o'
for dependency in ('tools/android-runtime/texture_probe.cpp','src/gpu/backend/vulkan/bounded_descriptors.h'):
    if (root / dependency).stat().st_mtime_ns > descriptor_object.stat().st_mtime_ns:
        raise SystemExit(f'Stale descriptor probe after editing {dependency}. Rebuild DescriptorProbe.')
shader_dependencies = {
    'texture_probe.spv': ['tools/android-runtime/texture_probe.comp'],
    'descriptor_probe.spv': ['tools/android-runtime/descriptor_probe.hlsl'],
    'clip_probe.vert.spv': ['tools/android-runtime/clip_probe.hlsl','src/gpu/backend/clip_interface.hlsli'],
    'clip_probe.frag.spv': ['tools/android-runtime/clip_probe.hlsl','src/gpu/backend/clip_interface.hlsli'],
    'clip_planes_gs.spv': ['src/gpu/backend/clip_planes.hlsl','src/gpu/backend/clip_interface.hlsli'],
}
for name, dependencies in shader_dependencies.items():
    binary = root / 'build/android-runtime' / name
    for dependency in dependencies:
        if (root / dependency).stat().st_mtime_ns > binary.stat().st_mtime_ns:
            raise SystemExit(f'Stale diagnostic shader {name} after editing {dependency}.')
for name, path in native.items():
    if not path.is_file(): raise SystemExit(f'Missing native artifact: {path}')
    shutil.copyfile(path, libs / name)
    run([llvm / 'llvm-strip.exe', '--strip-debug', libs / name])
for name in ('texture_probe.spv','clip_probe.vert.spv','clip_probe.frag.spv','clip_planes_gs.spv','descriptor_probe.spv'):
    shutil.copyfile(root / 'build/android-runtime' / name, assets / name)
run([java / 'javac.exe', '-encoding', 'UTF-8', '--release', '8',
     '-classpath', android, '-d', classes, *source.rglob('*.java'),
     *(root / 'sdk/thirdparty/sdl3/android-project/app/src/main/java').rglob('*.java')])
run([java / 'java.exe', '-cp', build_tools / 'lib/d8.jar', 'com.android.tools.r8.D8',
     '--min-api', '28', '--lib', android, '--output', dex, *classes.rglob('*.class')])
base = out / 'base.apk'
if base.exists(): base.unlink()
run([build_tools / 'aapt2.exe', 'link', '-o', base, '-I', android,
     '--manifest', source / 'AndroidManifest.xml', '-A', stage / 'assets'])
unsigned = out / 'unsigned.apk'
with zipfile.ZipFile(unsigned, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as apk, zipfile.ZipFile(base) as resources:
    for entry in resources.infolist():
        # API 30+ requires resources.arsc stored (not deflated), then aligned.
        apk.writestr(entry.filename, resources.read(entry), compress_type=entry.compress_type)
    apk.write(dex / 'classes.dex', 'classes.dex')
    for path in libs.iterdir(): apk.write(path, 'lib/arm64-v8a/' + path.name)
aligned = out / 'aligned.apk'
run([build_tools / 'zipalign.exe', '-f', '-P', '16', '4', unsigned, aligned])
key = out / 'debug.keystore'
if not key.exists():
    run([java / 'keytool.exe', '-genkeypair', '-keystore', key, '-storepass', 'android',
         '-keypass', 'android', '-alias', 'androiddebugkey', '-keyalg', 'RSA', '-keysize', '2048',
         '-validity', '3650', '-dname', 'CN=Android Debug,O=Broken Bond Research,C=US'])
apk = out / 'BrokenBond-Native-Test.apk'
run([java / 'java.exe', '-jar', build_tools / 'lib/apksigner.jar', 'sign', '--ks', key,
     '--ks-key-alias', 'androiddebugkey', '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
     '--out', apk, aligned])
run([java / 'java.exe', '-jar', build_tools / 'lib/apksigner.jar', 'verify', '--verbose', apk])
run([build_tools / 'zipalign.exe', '-c', '-P', '16', '4', apk])
with zipfile.ZipFile(apk) as package:
    assert not any(n.endswith('.xex') or n.endswith('.bf') for n in package.namelist())
    if 'resources.arsc' in package.namelist():
        assert package.getinfo('resources.arsc').compress_type == zipfile.ZIP_STORED
report = dict(apk=str(apk), bytes=apk.stat().st_size, sha256=hashlib.sha256(apk.read_bytes()).hexdigest(),
              package='org.brokenbond.nativecheck', version='0.4.0', target_api=35, min_api=28,
              architecture='arm64-v8a', signature_verified=True, zip_alignment_verified=True,
              game_assets_bundled=False, game_launch_available=True, original_game_boot_attempt_available=True,
              native_files={name:dict(source=str(path),packaged_bytes=(libs/name).stat().st_size,
                                     sha256=hashlib.sha256((libs/name).read_bytes()).hexdigest()) for name,path in native.items()})
(out / 'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='native_files'},indent=2))
