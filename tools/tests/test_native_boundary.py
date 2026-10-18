"""No compiler or Qt SDK is needed for these source-scanner fixtures."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_boundary import check_native_boundary


class NativeBoundaryTests(unittest.TestCase):
    def check_files(self, files):
        with tempfile.TemporaryDirectory(prefix='bottle-native-boundary-') as temp:
            root = Path(temp)
            for name, text in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8')
            return check_native_boundary(root)

    def test_qt_includes(self):
        for header in ('QObject', 'QString', 'QtCore/QObject', 'QtConcurrent', 'qobject.h', 'qt6/QtCore/QObject',
                       'qfile.h', 'qbytearray.h', 'qthread.h', 'qnetworkaccessmanager.h', 'qquickitem.h'):
            with self.subTest(header=header):
                self.assertEqual(len(self.check_files({'core/a.cpp': f'# include <{header}>\n'})), 1)

    def test_qt_cmake(self):
        for command in ('find_package(Qt6 REQUIRED COMPONENTS Core)',
                        'find_package(Qt6Core CONFIG REQUIRED)',
                        'find_package("Qt5" CONFIG REQUIRED)',
                        'target_link_libraries(EchoCore PRIVATE Qt6::Core)',
                        'target_link_libraries(EchoCore PRIVATE "$<LINK_ONLY:Qt6::Core>")',
                        'link_libraries("C:/SDK/lib/Qt6Core.lib")'):
            with self.subTest(command=command):
                self.assertEqual(len(self.check_files({'CMakeLists.txt': command})), 1)

    def test_comments_variables_and_fixture_strings(self):
        self.assertEqual(self.check_files({
            'core/a.cpp': '// #include <QObject>\n/*\n#include <QString>\n*/\nint QBuffer = 0;\n#include "Query.h"\n',
            'CMakeLists.txt': '# find_package(Qt6)\n#[=[\ntarget_link_libraries(A Qt6::Core)\n]=]\n'
                             'set(QBuffer "normal # variable")\n'
                             'check_fixture(example [=[\nfind_package(Qt6)\n]=])\n'
                             'target_link_libraries(A PRIVATE nlohmann_json::nlohmann_json # Qt6::Core\n)\n',
        }), [])

    def test_external_and_generated_directories(self):
        self.assertEqual(self.check_files({
            f'{directory}/a.cpp': '#include <QObject>'
            for directory in ('third_party', 'vendor', 'generated', 'output', 'vcpkg_installed', 'build-local')
        }), [])

    def test_raw_strings_preserve_following_include_and_line_number(self):
        for prefix in ('', 'u8', 'u', 'U', 'L'):
            for delimiter in ('', 'cpp', 'qt_fixture'):
                with self.subTest(prefix=prefix, delimiter=delimiter):
                    text = (f'const auto sample = {prefix}R"{delimiter}(\n'
                            '#include <QObject>\n#include "QString"\n'
                            f'){delimiter}";\n#include "QByteArray"\n')
                    errors = self.check_files({'core/a.cpp': text})
                    self.assertEqual(len(errors), 1)
                    self.assertIn('core/a.cpp:5: include QByteArray', errors[0].replace('\\', '/'))

    def test_native_headers_with_q_prefix_are_allowed(self):
        text = ''.join(f'#include "{header}"\n' for header in
                       ('query.h', 'qnative.h', 'qqueue_impl.h', 'qfile_reader.h', 'qbytearray_view.h'))
        self.assertEqual(self.check_files({'core/a.cpp': text}), [])


if __name__ == '__main__':
    unittest.main()
