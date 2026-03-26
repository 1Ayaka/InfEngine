import json, glob, os

for scene_path in glob.glob(r"e:\project\infengineProject\1\Assets\**\*.scene", recursive=True):
    with open(scene_path, "r", encoding="utf-8") as f:
        d = json.load(f)
    basename = os.path.basename(scene_path)
    for o in d.get("objects", []):
        pg = o.get("prefab_guid", "")
        pr = o.get("prefab_root", False)
        name = o.get("name", "?")
        if pg or pr:
            print(f"{basename}: PREFAB_INST name={name} guid={pg} root={pr}")
        for pc in o.get("py_components", []):
            pf = pc.get("py_fields", {})
            tn = pf.get("__type_name__", "")
            for k, v in pf.items():
                if isinstance(v, dict) and "__prefab_ref__" in v:
                    print(f"{basename}: PREFAB_REF in {tn}.{k} = {v}")
                if isinstance(v, dict) and "__game_object__" in v:
                    print(f"{basename}: GO_REF in {tn}.{k} = {v}")
            if tn:
                print(f"{basename}: py_comp obj={name} type={tn}")
