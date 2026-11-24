"""Behavioral tests for release scripts; Python is a development dependency only."""
import pathlib
import platform
import plistlib
import shutil
import sys
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]


@unittest.skipUnless(platform.system() == "Darwin", "macOS binary tools required")
class MacRuntimeTests(unittest.TestCase):
    def test_runtime_is_embedded_and_loads_without_original_library(self):
        # Break caught: absolute dependency left in the app, or dylib not copied/signed.
        with tempfile.TemporaryDirectory(prefix="sonicator runtime ") as directory:
            root = pathlib.Path(directory)
            bundle = root / "Test.app"
            executable = bundle / "Contents/MacOS/Test"
            executable.parent.mkdir(parents=True)
            source = root / "source"
            source.mkdir()
            (source / "lib.c").write_text("int value(void) { return 42; }\n")
            (source / "main.c").write_text("int value(void); int main(void) { return value() == 42 ? 0 : 1; }\n")
            library = source / "libonnxruntime.dylib"
            subprocess.run(["clang", "-arch", "arm64", "-arch", "x86_64", "-dynamiclib", str(source / "lib.c"),
                            "-install_name", "@rpath/libonnxruntime.dylib", "-o", str(library)], check=True)
            subprocess.run(["clang", "-arch", "arm64", "-arch", "x86_64", "-Wl,-headerpad_max_install_names",
                            "-Wl,-rpath," + str(source), str(source / "main.c"), str(library),
                            "-o", str(executable)], check=True)
            (bundle / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "Test"}))
            result = subprocess.run(["sh", str(ROOT / "cmake/EmbedMacRuntime.sh"),
                                     str(bundle), str(library), "Test"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            load_commands = subprocess.check_output(["otool", "-l", str(executable)], text=True)
            self.assertNotIn(str(source), load_commands, "Build-tree rpath retained ahead of embedded runtime")
            library.unlink()
            self.assertTrue((bundle / "Contents/Frameworks/libonnxruntime.dylib").is_file())
            loaded = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(loaded.returncode, 0, loaded.stderr)
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
            verified = subprocess.run(["sh", str(ROOT / "packaging/macos/verify-bundle.sh"), str(bundle)],
                                      capture_output=True, text=True)
            self.assertEqual(verified.returncode, 0, verified.stderr)

    def test_missing_runtime_fails_without_modifying_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            sentinel = root / "keep"
            sentinel.write_text("unchanged")
            result = subprocess.run(["sh", str(ROOT / "cmake/EmbedMacRuntime.sh"),
                                     str(root), str(root / "absent.dylib"), "Test"],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(sentinel.read_text(), "unchanged")


@unittest.skipUnless(platform.system() == "Darwin", "macOS installer tools required")
class PostinstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="sonicator pkg ")
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name).resolve()
        self.volume = self.root / "Target Volume"
        self.volume.mkdir()
        self.scripts = self.root / "scripts"
        self.scripts.mkdir()
        self.bundle = self.volume / "Applications/SONICATOR.app"
        (self.bundle / "Contents").mkdir(parents=True)
        (self.bundle / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleIdentifier": "com.sonicator.browser"}))
        self.media = self.volume / "Users/test/Library/Sonicator/exports/keep.wav"
        self.media.parent.mkdir(parents=True)
        self.media.write_bytes(b"project audio")
        subprocess.run(["xattr", "-w", "com.apple.quarantine", "0081;test;test;",
                        str(self.bundle / "Contents/Info.plist")], check=True)

    def run_script(self, component="app"):
        script = ROOT / "packaging/macos/postinstall"
        self.assertTrue(script.is_file(), "postinstall is not implemented")
        shutil.copyfile(script, self.scripts / "postinstall")
        (self.scripts / "component").write_text(component + "\n")
        return subprocess.run(["sh", str(self.scripts / "postinstall"), "package.pkg", "/",
                               str(self.volume)], capture_output=True, text=True)

    def test_selected_bundle_quarantine_removed_on_alternate_volume(self):
        subprocess.run(["xattr", "-w", "org.sonicator.test", "keep", str(self.bundle)], check=True)
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        attributes = subprocess.check_output(["xattr", "-r", str(self.bundle)], text=True)
        self.assertNotIn("com.apple.quarantine", attributes)
        self.assertIn("org.sonicator.test", attributes)
        self.assertEqual(self.media.read_bytes(), b"project audio")
        # Already clean install must also succeed.
        self.assertEqual(self.run_script().returncode, 0)

    def test_unselected_component_is_untouched(self):
        result = self.run_script("vst3")
        # VST3 is absent: do not claim successful fixup of a missing selected payload.
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("com.apple.quarantine", subprocess.check_output(
            ["xattr", "-r", str(self.bundle)], text=True))

    def test_wrong_identity_fails_without_removing_quarantine(self):
        (self.bundle / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleIdentifier": "com.someone.else"}))
        subprocess.run(["xattr", "-w", "com.apple.quarantine", "0081;test;test;", str(self.bundle)], check=True)
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("identity", result.stderr.lower())
        self.assertIn("com.apple.quarantine", subprocess.check_output(["xattr", str(self.bundle)], text=True))

    def test_symlinked_parent_is_rejected(self):
        applications = self.volume / "Applications"
        applications.rename(self.root / "elsewhere")
        applications.symlink_to(self.root / "elsewhere", target_is_directory=True)
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("symlink", result.stderr.lower())
        self.assertIn("com.apple.quarantine", subprocess.check_output(
            ["xattr", "-r", str(self.root / "elsewhere")], text=True))

    def test_symlinked_child_is_rejected(self):
        outside = self.root / "outside"
        outside.write_text("do not modify")
        subprocess.run(["xattr", "-w", "com.apple.quarantine", "0081;test;test;", str(outside)], check=True)
        (self.bundle / "Contents/escape").symlink_to(outside)
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("symlink", result.stderr.lower())
        self.assertIn("com.apple.quarantine", subprocess.check_output(["xattr", str(outside)], text=True))


class PackageInputTests(unittest.TestCase):
    @unittest.skipUnless(platform.system() == "Darwin", "macOS packaging required")
    def test_missing_payload_fails_before_creating_installer(self):
        script = ROOT / "packaging/macos/build-pkg.sh"
        self.assertTrue(script.is_file(), "PKG builder is not implemented")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            result = subprocess.run(["sh", str(script), str(root / "missing"), str(root / "out")],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(list(root.rglob("*.pkg")))

    def test_licence_collection_refuses_incomplete_dependencies(self):
        script = ROOT / "packaging/collect-licenses.py"
        self.assertTrue(script.is_file(), "Licence collection is not implemented")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            result = subprocess.run([sys.executable, str(script), str(root), str(root / "out")],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Missing licence", result.stderr)
            self.assertFalse((root / "out").exists())


class WindowsPayloadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="sonicator msi ")
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.stage = self.root / "stage"
        for relative in ["app/SONICATOR.exe", "app/onnxruntime.dll",
                         "vst3/Contents/x86_64-win/SONICATOR.vst3",
                         "vst3/Contents/x86_64-win/onnxruntime.dll",
                         "vst3/Contents/Resources/moduleinfo.json"]:
            target = self.stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"fixture")

    def generate(self, output="Payload.wxs"):
        script = ROOT / "packaging/windows/generate-payload.py"
        self.assertTrue(script.is_file(), "Windows payload generator is not implemented")
        return subprocess.run([sys.executable, str(script), str(self.stage), str(self.root / output)],
                              capture_output=True, text=True)

    def test_payload_paths_and_components_are_complete_and_stable(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        tree = ET.parse(self.root / "Payload.wxs")
        ns = {"w": "http://wixtoolset.org/schemas/v4/wxs"}
        groups = tree.findall(".//w:ComponentGroup", ns)
        self.assertEqual({g.attrib["Id"] for g in groups}, {"AppPayload", "Vst3Payload"})
        files = tree.findall(".//w:File", ns)
        self.assertEqual(len(files), 5)
        self.assertEqual(len(tree.findall(".//w:Shortcut", ns)), 1)
        before = [(c.attrib["Id"], c.attrib["Guid"]) for c in tree.findall(".//w:Component", ns)]
        # A new file sorts first: existing components must not get renumbered.
        (self.stage / "app/AAA.dll").write_bytes(b"new")
        self.assertEqual(self.generate("Next.wxs").returncode, 0)
        after = [(c.attrib["Id"], c.attrib["Guid"]) for c in ET.parse(self.root / "Next.wxs").findall(".//w:Component", ns)]
        self.assertTrue(set(before).issubset(set(after)))
        for file in files:
            self.assertTrue(pathlib.Path(file.attrib["Source"]).is_file())

    def test_missing_runtime_is_rejected(self):
        (self.stage / "vst3/Contents/x86_64-win/onnxruntime.dll").unlink()
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing", result.stderr)
        self.assertFalse((self.root / "Payload.wxs").exists())

    def test_symlink_is_rejected(self):
        (self.stage / "app/escape.dll").symlink_to(self.root / "external.dll")
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Symlink", result.stderr)

    def test_start_menu_shortcut_is_plain_and_targets_the_executable(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        tree = ET.parse(self.root / "Payload.wxs")
        ns = {"w": "http://wixtoolset.org/schemas/v4/wxs"}
        shortcuts = tree.findall(".//w:Shortcut", ns)
        self.assertEqual(len(shortcuts), 1)
        shortcut = shortcuts[0]
        # An advertised shortcut takes its icon from an MSI Icon row and shows a blank icon when
        # that row is missing; a plain shortcut uses the executable's embedded icon.
        self.assertNotEqual(shortcut.attrib.get("Advertise"), "yes")
        files = tree.findall(".//w:File[w:Shortcut]", ns)
        self.assertEqual(len(files), 1)
        self.assertTrue(files[0].attrib["Source"].endswith("SONICATOR.exe"))
        self.assertEqual(files[0].attrib.get("KeyPath"), "yes")
        # A shortcut is advertised through its component's attribute, never a Shortcut attribute.
        for component in tree.findall(".//w:Component", ns):
            self.assertNotEqual(component.attrib.get("Advertise"), "yes")


class WindowsIconTests(unittest.TestCase):
    def test_package_declares_one_icon_for_apps_and_features(self):
        tree = ET.parse(ROOT / "packaging/windows/Sonicator.wxs")
        ns = {"w": "http://wixtoolset.org/schemas/v4/wxs"}
        icons = tree.findall(".//w:Icon", ns)
        self.assertEqual(len(icons), 1, "the MSI must carry exactly one icon row")
        self.assertEqual(icons[0].attrib.get("Id"), "AppIcon.ico")
        self.assertEqual(icons[0].attrib.get("SourceFile"), "$(var.AppIcon)")
        properties = {p.attrib.get("Id"): p.attrib.get("Value")
                      for p in tree.findall(".//w:Property", ns)}
        self.assertEqual(properties.get("ARPPRODUCTICON"), "AppIcon.ico")

    def test_executable_icon_comes_from_the_branding_png(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('ICON_BIG "${SONICATOR_APP_ICON}"', cmake)
        # Windows takes the trimmed canvas: the shell draws the icon at 16-48 px beside icons that
        # fill their box, so a 25 % transparent margin reads as a smaller icon.
        self.assertIn("resources/branding/icon-windows-1024.png", cmake)
        for name in ("icon-windows-1024.png", "icon-window-256.png"):
            branding = ROOT / "resources/branding" / name
            self.assertTrue(branding.is_file(), name)
            with branding.open("rb") as stream:
                self.assertEqual(stream.read(8), b"\x89PNG\r\n\x1a\n")

    def test_msi_builder_requires_the_generated_windows_icon(self):
        script = (ROOT / "packaging/windows/build-msi.ps1").read_text(encoding="utf-8")
        self.assertIn("JuceLibraryCode/icon.ico", script)
        self.assertIn('AppIcon=$Stage/app-icon.ico', script)


if __name__ == "__main__":
    unittest.main()
