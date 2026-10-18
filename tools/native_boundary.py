"""Source-level checks for obvious Qt dependencies in maintained Native files."""
from pathlib import Path
import re

_SKIP = {'out', 'output', 'build', 'CMakeFiles', 'vcpkg_installed', '__pycache__',
         '.git', 'third_party', 'third-party', 'vendor', 'generated', '_deps'}
_CPP = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx'}
_CPP_TOKEN = re.compile(
    r'(?P<raw>(?<!\w)(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)")'
    r'|//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"\n]+)', re.M)
_QT_HEADER = re.compile(r'^(?:qt[456]/)?(?:Qt[A-Z][\w/.-]*|Q[A-Z]\w*(?:\.h)?)$')
# Lowercase Qt headers have no universal prefix distinct from Native headers.
# Keep an explicit set of common public headers rather than reject every q*.h.
_QT_LOWER_HEADERS = {f'{name}.h' for name in '''
    qglobal qtypes qobjectdefs qnamespace qtmetamacros qassert qlogging qdebug
    qbytearray qbytearrayview qstring qstringview qlatin1stringview qanystringview
    qstringlist qchar qvariant qmetatype qmetaobject qobject qcoreapplication
    qeventloop qevent qthread qthreadpool qrunnable qfuture qfuturewatcher qpromise
    qatomic qmutex qreadwritelock qsemaphore qwaitcondition qdeadlinetimer qtimer
    qelapsedtimer qdatetime qdate qtime qtimezone qfile qfileinfo qdir qdiriterator
    qiodevice qbuffer qdatastream qtextstream qsavefile qtemporaryfile qtemporarydir
    qresource qsettings qstandardpaths qurl quuid qjsondocument qjsonobject
    qjsonarray qjsonvalue qcborvalue qregularexpression qcryptographichash qsysinfo
    qprocess qlibrary qplugin qpointer qsharedpointer qscopedpointer qvector qlist
    qmap qhash qset qqueue qstack qpair qvarlengtharray qabstractitemmodel
    qimage qpixmap qcolor qfont qpainter qguiapplication qwindow qscreen
    qnetworkaccessmanager qnetworkreply qnetworkrequest qnetworkcookie qtcpsocket
    qudpsocket qhostaddress qaudio qaudiodevice qaudiosink qaudiooutput qmediaplayer
    qmediadevices qaudiodecoder qqmlengine qqmlapplicationengine qqmlcontext
    qqmlcomponent qquickitem qquickview qquickwindow qjsengine qjsvalue
    qsqldatabase qsqlquery qsqlerror qtconcurrentrun
'''.split()}
_QT_LINK = re.compile(r'(?<![\w])Qt[456]?::|(?<![\w])(?:-l)?(?:lib)?Qt[456][A-Z]\w*(?:\.(?:lib|dll|a|dylib|so(?:\.\d+)*))?(?![\w])', re.I)
_CMAKE_TOKEN = re.compile(
    r'(?P<comment>#\[(?P<ceq>=*)\[.*?\](?P=ceq)\]|#[^\n]*)'
    r'|(?P<bracket>\[(?P<beq>=*)\[.*?\](?P=beq)\])'
    r'|(?P<quoted>"(?:\\.|[^"\\])*")'
    r'|(?P<word>[A-Za-z_][A-Za-z0-9_]*)|(?P<paren>[()])', re.S)


def _without_comments(text):
    return _CPP_TOKEN.sub(lambda m: re.sub(r'[^\n]', ' ', m[0])
                          if m.lastgroup == 'raw' or m[0].startswith(('//', '/*')) else m[0], text)


def _cmake_calls(text):
    """Read commands without interpreting code examples in quoted/bracket text."""
    pending = None
    depth = 0
    for token in _CMAKE_TOKEN.finditer(text):
        kind = token.lastgroup
        if kind == 'comment':
            continue
        if depth:
            if kind == 'paren':
                depth += 1 if token[0] == '(' else -1
                if depth == 0:
                    yield command, text[start:token.start()], line
            continue
        if kind == 'word':
            pending = (token[0].lower(), token.start())
        elif kind == 'paren' and token[0] == '(' and pending:
            command, offset = pending
            line = text.count('\n', 0, offset) + 1
            start = token.end()
            depth = 1
            pending = None
        else:
            pending = None


def _files(directory):
    for path in directory.iterdir():
        if path.name in _SKIP or path.name.startswith('build-'):
            continue
        if path.is_dir():
            yield from _files(path)
        else:
            yield path


def check_native_boundary(directory: Path):
    errors = []
    for path in _files(directory):
        relative = path.relative_to(directory)
        if path.suffix in _CPP:
            text = _without_comments(path.read_text(encoding='utf-8-sig'))
            for match in _INCLUDE.finditer(text):
                if _QT_HEADER.match(match[1]) or match[1] in _QT_LOWER_HEADERS:
                    line = text.count('\n', 0, match.start()) + 1
                    errors.append(f'Native Zero-Qt boundary: {relative}:{line}: include {match[1]}')
        elif path.name == 'CMakeLists.txt' or path.suffix == '.cmake':
            text = path.read_text(encoding='utf-8-sig')
            for command, args, line in _cmake_calls(text):
                args = _CMAKE_TOKEN.sub(lambda m: '' if m.lastgroup == 'comment' else m[0], args)
                if command == 'find_package':
                    package = re.match(r'\s*"?Qt(?:[456](?:[A-Za-z]\w*)?)?(?:"|\s|$)', args, re.I)
                    forbidden = bool(package)
                elif command in {'target_link_libraries', 'link_libraries'}:
                    forbidden = bool(_QT_LINK.search(args))
                else:
                    forbidden = False
                if forbidden:
                    errors.append(f'Native Zero-Qt boundary: {relative}:{line}: {command}')
    return errors
