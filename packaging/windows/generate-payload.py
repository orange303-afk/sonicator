#!/usr/bin/env python3
"""Generate one stable MSI component per file; preserve the VST3 bundle hierarchy."""
import hashlib
import pathlib
import sys
import uuid
import xml.etree.ElementTree as ET

stage, output = (pathlib.Path(p).resolve() for p in sys.argv[1:])
namespace = "http://wixtoolset.org/schemas/v4/wxs"
ET.register_namespace("", namespace)


def element(parent, name, **attributes):
    return ET.SubElement(parent, f"{{{namespace}}}{name}", attributes)


def identifier(prefix, value):
    return prefix + hashlib.sha256(value.encode("utf-8")).hexdigest()[:32]


required = ["app/SONICATOR.exe", "app/onnxruntime.dll",
            "vst3/Contents/x86_64-win/SONICATOR.vst3",
            "vst3/Contents/x86_64-win/onnxruntime.dll",
            "vst3/Contents/Resources/moduleinfo.json"]
for path in stage.rglob("*"):
    if path.is_symlink():
        sys.exit(f"Symlink in MSI payload: {path}")
for relative in required:
    if not (stage / relative).is_file():
        sys.exit(f"Missing MSI payload: {relative}")
root = ET.Element(f"{{{namespace}}}Wix")
for folder, directory_id, group_id in [("app", "INSTALLFOLDER", "AppPayload"),
                                        ("vst3", "VST3BUNDLE", "Vst3Payload")]:
    fragment = element(root, "Fragment")
    directory = element(fragment, "DirectoryRef", Id=directory_id)
    group = element(fragment, "ComponentGroup", Id=group_id)
    directories = {".": directory}
    names = set()
    for path in sorted((stage / folder).rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(stage / folder)
        key = folder + "/" + relative.as_posix().lower()
        if key in names:
            sys.exit(f"Case-insensitive MSI path collision: {relative}")
        names.add(key)
        current = directory
        current_path = pathlib.PurePosixPath()
        for part in relative.parts[:-1]:
            current_path /= part
            directory_key = current_path.as_posix()
            if directory_key not in directories:
                directories[directory_key] = element(current, "Directory",
                    Id=identifier("d", folder + "/" + directory_key.lower()), Name=part)
            current = directories[directory_key]
        component_id = identifier("c", key)
        # Stable namespace + install-relative path: adding files never changes existing GUIDs.
        guid = str(uuid.uuid5(uuid.UUID("7a6a7323-1f2c-4566-9dd9-23025bb7c35e"), key)).upper()
        component = element(current, "Component", Id=component_id, Guid=guid, Bitness="always64")
        file = element(component, "File", Id=identifier("f", key), Source=str(path), KeyPath="yes")
        if key == "app/sonicator.exe":
            # Not advertised: an advertised shortcut shows an icon only when the MSI carries an
            # Icon row for it, while a plain shortcut uses the icon embedded in the executable.
            # The executable icon is also the one a taskbar pin keeps across major upgrades.
            element(file, "Shortcut", Id="StartMenuShortcut", Directory="ProgramMenuFolder",
                    Name="SONICATOR", WorkingDirectory="INSTALLFOLDER")
        element(group, "ComponentRef", Id=component_id)
ET.indent(root)
ET.ElementTree(root).write(output, encoding="utf-8", xml_declaration=True)
