import sys
sys.dont_write_bytecode = True

import os
import json
import hashlib
import importlib
import shutil
import subprocess

Import("env")

project_dir = env.subst("$PROJECT_DIR")

ANSI_GREEN  = "\033[32m"
ANSI_YELLOW = "\033[33m"
ANSI_RESET  = "\033[0m"

CACHE_VERSION = 3
CACHE_PATH    = os.path.join(project_dir, ".pio", "pip3d_assetdb.json")

CHUNK_SUFFIX            = "_chunk"
BUILTIN_ENGINE_MODELS   = {"suzanne", "teapot", "bunny"}
BUILTIN_ENGINE_TEXTURES = {"barrier", "concrete", "gravel", "sun", "tile", "missing"}

GEN_DEPS = {
    "textures": ["PIL"],
    "sun":      ["PIL", "numpy"],
    "sky":      ["numpy", "PIL"],
    "audio":    ["numpy", "miniaudio"],
}


def _tag(color, msg):
    return f"{color}[Pip3D]{ANSI_RESET} {msg}"


def ensure_pip_packages(modules):
    missing = []
    for mod in modules:
        try:
            importlib.import_module(mod)
        except ImportError:
            missing.append("Pillow" if mod == "PIL" else mod)
    if not missing:
        return
    print(_tag(ANSI_YELLOW, f"Installing Python dependencies: {', '.join(missing)}"))
    try:
        subprocess.check_call([env.subst("$PYTHONEXE"), "-m", "pip", "install"] + missing)
    except (OSError, subprocess.CalledProcessError) as e:
        raise SystemExit(
            _tag(ANSI_YELLOW, f"pip install failed ({e}). "
                              f"Install manually: pip install {' '.join(missing)}"))
    for mod in missing:
        importlib.import_module(mod)


FILES_DB = {}


def file_hash(path, chunk=1 << 16):
    try:
        st = os.stat(path)
    except OSError:
        return None
    key = os.path.normcase(os.path.abspath(path))
    rec = FILES_DB.get(key)
    if rec and rec[0] == st.st_mtime_ns and rec[1] == st.st_size:
        return rec[2]
    h = hashlib.sha256()
    try:
        with open(path, "rb") as f:
            while True:
                buf = f.read(chunk)
                if not buf:
                    break
                h.update(buf)
    except OSError:
        return None
    digest = h.hexdigest()
    FILES_DB[key] = (st.st_mtime_ns, st.st_size, digest)
    return digest


def load_cache():
    try:
        with open(CACHE_PATH, "r", encoding="utf-8-sig") as f:
            data = json.load(f)
        if data.get("version") != CACHE_VERSION:
            return {}, {}
        files = {k: tuple(v) for k, v in data.get("files", {}).items()}
        return data.get("entries", {}), files
    except (OSError, ValueError):
        return {}, {}


def save_cache(entries):
    os.makedirs(os.path.dirname(CACHE_PATH), exist_ok=True)
    payload = {
        "version": CACHE_VERSION,
        "files": {k: list(v) for k, v in FILES_DB.items() if os.path.exists(k)},
        "entries": entries,
    }
    tmp = CACHE_PATH + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
        os.replace(tmp, CACHE_PATH)
    except OSError:
        pass


def make_fingerprint(script_paths, source_paths=(), extra=""):
    h = hashlib.sha256()
    for p in list(script_paths) + list(source_paths):
        h.update((file_hash(p) or "-").encode("utf-8"))
        h.update(b"\x00")
    h.update(extra.encode("utf-8"))
    return h.hexdigest()


def obj_material_deps(obj_path):
    deps = []
    stem = os.path.splitext(obj_path)[0]
    same_stem = stem + ".mtl"
    if os.path.isfile(same_stem):
        deps.append(same_stem)
    else:
        try:
            with open(obj_path, "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    parts = line.strip().split()
                    if parts and parts[0] == "mtllib":
                        cand = os.path.join(os.path.dirname(obj_path), " ".join(parts[1:]))
                        if os.path.isfile(cand):
                            deps.append(cand)
                        break
        except OSError:
            pass
    return deps


def needs_rebuild(entries, key, fingerprint, output_path):
    if not os.path.exists(output_path):
        return True
    rec = entries.get(key)
    if not rec or rec.get("fingerprint") != fingerprint:
        return True
    out_hash = file_hash(output_path)
    return out_hash is None or rec.get("output_hash") != out_hash


def mark_built(entries, key, fingerprint, output_path):
    entries[key] = {
        "fingerprint": fingerprint,
        "output_hash": file_hash(output_path) or "",
    }


def touch(path):
    try:
        os.utime(path, None)
    except OSError:
        pass


def parse_model_asset_name(raw_name):
    name = os.path.splitext(raw_name)[0]
    if name.lower().endswith(CHUNK_SUFFIX):
        clean = name[:-len(CHUNK_SUFFIX)]
        return clean if clean else name
    return name


def parse_tex_asset_name(raw_name):
    name  = os.path.splitext(raw_name)[0]
    parts = name.split("_")
    if len(parts) > 1:
        try:
            val = int(parts[-1])
            if val >= 16 and (val & (val - 1)) == 0:
                return "_".join(parts[:-1]), val
        except ValueError:
            pass
    return name, None


def run_convert(script_path, args):
    env_clean = dict(os.environ)
    env_clean["PYTHONDONTWRITEBYTECODE"] = "1"
    subprocess.check_call(
        [env.subst("$PYTHONEXE"), script_path] + args,
        env=env_clean,
    )


def parse_screen_resolution():
    width  = 480
    height = 320
    try:
        flags_text = env.GetProjectOption("build_flags", "")
        for token in flags_text.replace(",", "\n").split():
            if token.startswith("-DPIP3D_SCREEN_WIDTH="):
                width = int(token.split("=", 1)[1])
            elif token.startswith("-DPIP3D_SCREEN_HEIGHT="):
                height = int(token.split("=", 1)[1])
    except Exception:
        pass
    return width, height


CACHE, _FILES_LOADED = load_cache()
FILES_DB.update(_FILES_LOADED)
CACHE_CHANGED = False
LIVE_KEYS     = set()


def asset_is_current(key, fingerprint, output_path):
    LIVE_KEYS.add(key)
    return not needs_rebuild(CACHE, key, fingerprint, output_path)


def asset_mark_built(key, fingerprint, output_path):
    global CACHE_CHANGED
    mark_built(CACHE, key, fingerprint, output_path)
    touch(output_path)
    CACHE_CHANGED = True


models_dir        = os.path.join(project_dir, "Tools", "Models")
obj_sources_dir   = os.path.join(models_dir, "Asset")
models_convert_py = os.path.join(models_dir, "Convert.py")

engine_models_dir = os.path.join(project_dir, "lib", "Pip3D", "Pip3D", "Geometry", "Models")
app_models_dir    = os.path.join(project_dir, "src", "Models")

os.makedirs(engine_models_dir, exist_ok=True)
os.makedirs(app_models_dir, exist_ok=True)

expected_engine_models = {}
expected_app_models    = {}
pending_models         = []

if os.path.isdir(obj_sources_dir):
    for file in os.listdir(obj_sources_dir):
        if file.lower().endswith(".obj"):
            obj_path   = os.path.join(obj_sources_dir, file)
            clean_name = parse_model_asset_name(file)

            is_engine = clean_name.lower() in BUILTIN_ENGINE_MODELS
            dest_dir  = engine_models_dir if is_engine else app_models_dir
            hpp_path  = os.path.join(dest_dir, clean_name + ".hpp")

            if is_engine:
                expected_engine_models[os.path.basename(hpp_path)] = True
            else:
                expected_app_models[os.path.basename(hpp_path)] = True

            key = "model:" + file
            fingerprint = make_fingerprint([models_convert_py], [obj_path] + obj_material_deps(obj_path), "obj2mesh")
            if not asset_is_current(key, fingerprint, hpp_path):
                dest_label = "Engine (Geometry/Models)" if is_engine else "App (src/Models)"
                print(_tag(ANSI_GREEN, f"Building model: {file} -> {dest_label}/{clean_name}.hpp"))
                pending_models.append((obj_path, hpp_path, key, fingerprint))

if pending_models:
    batch_args = []
    for obj_path, hpp_path, _, _ in pending_models:
        batch_args += [obj_path, hpp_path]
    run_convert(models_convert_py, batch_args)
    for _, hpp_path, key, fingerprint in pending_models:
        asset_mark_built(key, fingerprint, hpp_path)

if os.path.isdir(engine_models_dir):
    for existing in os.listdir(engine_models_dir):
        if existing.lower().endswith(".hpp"):
            base_name = os.path.splitext(existing)[0].lower()
            if base_name not in BUILTIN_ENGINE_MODELS:
                stale_path = os.path.join(engine_models_dir, existing)
                try:
                    os.remove(stale_path)
                    print(_tag(ANSI_YELLOW, f"Removed non-engine model from lib/Pip3D: {existing}"))
                except OSError:
                    pass

if os.path.isdir(app_models_dir):
    for existing in os.listdir(app_models_dir):
        if existing.lower().endswith(".hpp") and existing not in expected_app_models:
            stale_path = os.path.join(app_models_dir, existing)
            try:
                os.remove(stale_path)
                print(_tag(ANSI_YELLOW, f"Removed orphaned app model header: {existing}"))
            except OSError:
                pass


textures_dir       = os.path.join(project_dir, "Tools", "Textures")
tex_sources_dir    = os.path.join(textures_dir, "Asset")
textures_convert_py = os.path.join(textures_dir, "Convert.py")

engine_textures_dir   = os.path.join(project_dir, "lib", "Pip3D", "Pip3D", "Rendering", "Resources", "Textures")
app_textures_dir      = os.path.join(project_dir, "src", "Textures")

os.makedirs(engine_textures_dir, exist_ok=True)
os.makedirs(app_textures_dir, exist_ok=True)

expected_engine_textures = {}
expected_app_textures    = {}
pending_textures         = []

if os.path.isdir(tex_sources_dir):
    claims = {}
    for file in sorted(os.listdir(tex_sources_dir)):
        if file.startswith("_"):
            continue
        if file.lower().endswith((".png", ".jpg", ".jpeg")):
            clean_name, _ = parse_tex_asset_name(file)
            claims.setdefault(clean_name, []).append(file)

    for clean_name, sources in claims.items():
        is_engine = clean_name.lower() in BUILTIN_ENGINE_TEXTURES
        dest_dir  = engine_textures_dir if is_engine else app_textures_dir
        hpp_path  = os.path.join(dest_dir, clean_name + ".hpp")

        if is_engine:
            expected_engine_textures[os.path.basename(hpp_path)] = True
        else:
            expected_app_textures[os.path.basename(hpp_path)] = True

        if len(sources) > 1:
            chosen   = sorted(sources)[-1]
            img_path = os.path.join(tex_sources_dir, chosen)
            print(_tag(ANSI_YELLOW, f"Name collision for '{clean_name}': {sources} -> using '{chosen}'"))
        else:
            img_path = os.path.join(tex_sources_dir, sources[0])

        key = "tex:" + clean_name
        fingerprint = make_fingerprint([textures_convert_py], [img_path], "png2tex")
        if not asset_is_current(key, fingerprint, hpp_path):
            dest_label = "Engine (Resources/Textures)" if is_engine else "App (src/Textures)"
            print(_tag(ANSI_GREEN, f"Building texture: {sources[-1]} -> {dest_label}/{clean_name}.hpp"))
            pending_textures.append((img_path, hpp_path, key, fingerprint))

if pending_textures:
    ensure_pip_packages(GEN_DEPS["textures"])
    batch_args = []
    for img_path, hpp_path, _, _ in pending_textures:
        batch_args += [img_path, hpp_path]
    run_convert(textures_convert_py, batch_args)
    for _, hpp_path, key, fingerprint in pending_textures:
        asset_mark_built(key, fingerprint, hpp_path)

sun_hpp_path = os.path.join(engine_textures_dir, "Sun.hpp")
sungen_path = os.path.join(textures_dir, "Sungen.py")
if os.path.isfile(sungen_path):
    expected_engine_textures["Sun.hpp"] = True
    sun_key = "sungen:sun"
    sun_fingerprint = make_fingerprint([sungen_path])
    if not asset_is_current(sun_key, sun_fingerprint, sun_hpp_path):
        print(_tag(ANSI_GREEN, f"Building sun texture: Sungen.py -> Sun.hpp"))
        ensure_pip_packages(GEN_DEPS["sun"])
        run_convert(sungen_path, [sun_hpp_path])
        asset_mark_built(sun_key, sun_fingerprint, sun_hpp_path)

missing_hpp_path = os.path.join(engine_textures_dir, "Missing.hpp")
missinggen_path  = os.path.join(textures_dir, "Missinggen.py")
if os.path.isfile(missinggen_path):
    expected_engine_textures["Missing.hpp"] = True
    missing_key = "missinggen:missing"
    missing_fingerprint = make_fingerprint([missinggen_path])
    if not asset_is_current(missing_key, missing_fingerprint, missing_hpp_path):
        print(_tag(ANSI_GREEN, f"Building fallback texture: Missinggen.py -> Missing.hpp"))
        run_convert(missinggen_path, [missing_hpp_path])
        asset_mark_built(missing_key, missing_fingerprint, missing_hpp_path)

if os.path.isdir(engine_textures_dir):
    for existing in os.listdir(engine_textures_dir):
        if existing.lower().endswith(".hpp"):
            base_name = os.path.splitext(existing)[0].lower()
            if base_name not in BUILTIN_ENGINE_TEXTURES or existing not in expected_engine_textures:
                stale_path = os.path.join(engine_textures_dir, existing)
                try:
                    os.remove(stale_path)
                    print(_tag(ANSI_YELLOW, f"Removed non-engine texture from lib/Pip3D: {existing}"))
                except OSError:
                    pass

if os.path.isdir(app_textures_dir):
    for existing in os.listdir(app_textures_dir):
        if existing.lower().endswith(".hpp") and existing not in expected_app_textures:
            stale_path = os.path.join(app_textures_dir, existing)
            try:
                os.remove(stale_path)
                print(_tag(ANSI_YELLOW, f"Removed orphaned app texture header: {existing}"))
            except OSError:
                pass


skygen_path     = os.path.join(project_dir, "Tools", "Textures", "Skygen.py")
clouds_hpp_path = os.path.join(project_dir, "lib", "Pip3D", "Pip3D", "Rendering", "Environment", "CloudsData.hpp")

if os.path.isfile(skygen_path):
    screen_w, screen_h = parse_screen_resolution()
    sky_key = f"skygen:cloudsdata:{screen_w}x{screen_h}"
    sky_fingerprint = make_fingerprint([skygen_path], extra=f"{screen_w}x{screen_h}")
    if not asset_is_current(sky_key, sky_fingerprint, clouds_hpp_path):
        print(_tag(ANSI_GREEN, f"Building cloud data: Skygen.py -> CloudsData.hpp ({screen_w}x{screen_h})"))
        ensure_pip_packages(GEN_DEPS["sky"])
        run_convert(skygen_path, [clouds_hpp_path, "--screen-w", str(screen_w), "--screen-h", str(screen_h)])
        asset_mark_built(sky_key, sky_fingerprint, clouds_hpp_path)


audio_dir          = os.path.join(project_dir, "Tools", "Audio")
audio_sources_dir  = os.path.join(audio_dir, "Asset")
audio_convert_py   = os.path.join(audio_dir, "Convert.py")
sounds_output_dir  = os.path.join(project_dir, "lib", "Pip3D", "Pip3D", "Audio", "Sounds")

os.makedirs(sounds_output_dir, exist_ok=True)
expected_audio_outputs = {}
pending_audio          = []

if os.path.isdir(audio_sources_dir):
    audio_claims = {}
    for file in sorted(os.listdir(audio_sources_dir)):
        if file.startswith("_"):
            continue
        if file.lower().endswith((".wav", ".mp3", ".ogg", ".flac")):
            clean_name, _ = parse_tex_asset_name(file)
            audio_claims.setdefault(clean_name, []).append(file)

    for clean_name, sources in audio_claims.items():
        hpp_path = os.path.join(sounds_output_dir, clean_name + ".hpp")
        expected_audio_outputs[os.path.basename(hpp_path)] = True

        if len(sources) > 1:
            chosen = sorted(sources)[-1]
            src_path = os.path.join(audio_sources_dir, chosen)
            print(_tag(ANSI_YELLOW, f"Name collision for audio '{clean_name}': {sources} -> using '{chosen}'"))
        else:
            src_path = os.path.join(audio_sources_dir, sources[0])

        key = "audio:" + clean_name
        fingerprint = make_fingerprint([audio_convert_py], [src_path], "audio")
        if not asset_is_current(key, fingerprint, hpp_path):
            print(_tag(ANSI_GREEN, f"Building audio: {sources[-1]} -> {clean_name}.hpp"))
            pending_audio.append((src_path, hpp_path, key, fingerprint))

if pending_audio:
    ensure_pip_packages(GEN_DEPS["audio"])
    batch_args = []
    for src_path, hpp_path, _, _ in pending_audio:
        batch_args += [src_path, hpp_path]
    run_convert(audio_convert_py, batch_args)
    for _, hpp_path, key, fingerprint in pending_audio:
        asset_mark_built(key, fingerprint, hpp_path)

if os.path.isdir(sounds_output_dir):
    for existing in os.listdir(sounds_output_dir):
        if existing.lower().endswith(".hpp") and existing not in expected_audio_outputs:
            stale_path = os.path.join(sounds_output_dir, existing)
            try:
                os.remove(stale_path)
                print(_tag(ANSI_YELLOW, f"Removed orphaned audio header: {existing}"))
            except OSError:
                pass

stale_keys = [k for k in CACHE if k not in LIVE_KEYS and not k.startswith("bake:")]
for k in stale_keys:
    del CACHE[k]

if CACHE_CHANGED or stale_keys:
    save_cache(CACHE)

def _is_bake_up_to_date():
    try:
        bake_cache = os.path.join(project_dir, "Tools", "Bake", "Build", "bake_cache.json")
        scene = (os.environ.get("PIP3D_BAKE_SCENE") or "Scene").strip() or "Scene"
        baked = os.path.join(project_dir, "src", "Lighting", f"Baked{scene}.hpp")
        if scene == "Scene" and not os.path.exists(baked):
            baked = os.path.join(project_dir, "src", "Lighting", "BakedScene.hpp")
        if not os.path.exists(baked) or not os.path.exists(bake_cache):
            return False
        try:
            baked_mtime = os.path.getmtime(baked)
            cache_mtime = os.path.getmtime(bake_cache)
            newest_input = 0
            bases = [os.path.join(project_dir, "Tools", "Bake", "Source"),
                     os.path.join(project_dir, "src"),
                     os.path.join(project_dir, "lib", "Pip3D")]
            for base in bases:
                if not os.path.isdir(base):
                    continue
                for r, _, fs in os.walk(base):
                    for fn in fs:
                        fp = os.path.join(r, fn)
                        if fn.startswith("Baked") and os.path.basename(r) == "Lighting":
                            continue
                        try:
                            mt = os.path.getmtime(fp)
                            if mt > newest_input:
                                newest_input = mt
                        except OSError:
                            pass
            try:
                mt = os.path.getmtime(os.path.join(project_dir, "Tools", "Bake", "Bake.ps1"))
                if mt > newest_input:
                    newest_input = mt
            except OSError:
                pass
            if newest_input > baked_mtime or newest_input > cache_mtime:
                return False
        except OSError:
            return False
        try:
            j = json.load(open(bake_cache, "r", encoding="utf-8-sig"))
            if j.get("version") != 1 or not j.get("fingerprint"):
                return False
            oh = file_hash(baked)
            if not oh or j.get("output_hash") != oh:
                return False
        except Exception:
            return False
        return True
    except Exception:
        return False

try:
    bake_src_dir = os.path.join(project_dir, "Tools", "Bake", "Source")
    bake_ps1 = os.path.join(project_dir, "Tools", "Bake", "Bake.ps1")
    if os.path.isdir(bake_src_dir) and os.path.isfile(bake_ps1) and os.path.isfile(os.path.join(project_dir, "src", "main.cpp")):
        if _is_bake_up_to_date():
            print(_tag(ANSI_YELLOW, "Bake up-to-date — skipping Bake.ps1 (use -Clean to force)"))
        else:
            _pwsh = shutil.which("powershell") or shutil.which("pwsh")
            if _pwsh:
                print(_tag(ANSI_GREEN, "Triggering Bake.ps1 ..."))
                try:
                    subprocess.check_call([_pwsh, "-ExecutionPolicy", "Bypass", "-File", bake_ps1], env=dict(os.environ))
                except subprocess.CalledProcessError as e:
                    print(_tag(ANSI_YELLOW, f"Bake.ps1 failed with code {e.returncode}"))
            else:
                print(_tag(ANSI_YELLOW, "Powershell not found, skipping Bake.ps1"))
except Exception as _ex:
    print(_tag(ANSI_YELLOW, f"Bake trigger error: {_ex}"))
