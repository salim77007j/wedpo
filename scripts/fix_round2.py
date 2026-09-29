# Wedpo compile fixes round 2 — written as a file to avoid heredoc escaping issues
import io

BS = chr(92)  # backslash

# ---------- app/src/webpage.cpp ----------
p = 'app/src/webpage.cpp'
s = io.open(p, encoding='utf-8').read()

# 1. missing include
if '#include <QWebEngineCertificateError>' not in s:
    s = s.replace('#include <QWebEngineSettings>',
                  '#include <QWebEngineSettings>\n#include <QWebEngineCertificateError>')

# 2. replace corrupted jsStringLiteral helper entirely (between markers)
start = s.index('static QString jsStringLiteral')
end = s.index('WebPage::WebPage(')
DQ = chr(34)  # double quote
lines = []
lines.append('static QString jsStringLiteral(const QString &s)')
lines.append('{')
lines.append('    QString out;')
lines.append('    out.reserve(s.size() + 16);')
lines.append('    out.append(' + DQ + BS + DQ + ');')
lines.append('    for (const QChar c : s) {')
lines.append('        switch (c.unicode()) {')
lines.append('        case 0x22:  out.append(' + DQ + BS + BS + DQ + DQ + '); break;')
lines.append('        case 0x5C:  out.append(' + DQ + BS + BS + BS + BS + DQ + '); break;')
lines.append('        case 0x0A: out.append(' + DQ + BS + 'n' + DQ + '); break;')
lines.append('        case 0x0D: out.append(' + DQ + BS + 'r' + DQ + '); break;')
lines.append('        case 0x09: out.append(' + DQ + BS + 't' + DQ + '); break;')
lines.append('        default:')
lines.append('            if (c.unicode() < 0x20)')
lines.append('                out.append(QStringLiteral(' + DQ + BS + 'u%1' + DQ
             + ').arg(c.unicode(), 4, 16, QLatin1Char(0)));')
lines.append('            else')
lines.append('                out.append(c);')
lines.append('        }')
lines.append('    }')
lines.append('    out.append(' + DQ + BS + DQ + ');')
lines.append('    return out;')
lines.append('}')
lines.append('')
s = s[:start] + '\n'.join(lines) + s[end:]

# 3. view() removed from QWebEnginePage in Qt 6.8 -> use parent widget
s = s.replace('QMessageBox box(this->view());',
              'QWidget *dlgParent = qobject_cast<QWidget *>(parent());\n    QMessageBox box(dlgParent);')

# 4. onCertificateError: drop stale early-exit block and bool returns in void fn
s = s.replace(
    '    if (!ce.isOverridable())\n        return false;   // reject hard errors outright\n\n    const QUrl url',
    '    const QUrl url')
s = s.replace(
    '        ce.acceptCertificate();\n        return true;\n    }\n    if (!ce.isOverridable()) {',
    '        ce.acceptCertificate();\n        return;\n    }\n    if (!ce.isOverridable()) {')

io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('webpage.cpp patched; view() left:', s.count('this->view()'),
      '; return true left:', s.count('return true;'))

# ---------- app/src/tabstrip.cpp ----------
p = 'app/src/tabstrip.cpp'
s = io.open(p, encoding='utf-8').read()
s = s.replace('        m_spinnerAngle = qNormalizeAngle(m_spinnerAngle + 30);',
              '        m_spinnerAngle = std::fmod(m_spinnerAngle + 30.0, 360.0);')
s = s.replace('            const QRect ar = audioRect(r);',
              '            QRect ar = audioRect(r);')
if '#include <cmath>' not in s:
    s = s.replace('#include <algorithm>', '#include <algorithm>\n#include <cmath>')
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('tabstrip.cpp patched')

# ---------- app/src/bridge.cpp ----------
p = 'app/src/bridge.cpp'
s = io.open(p, encoding='utf-8').read()
n = s.count('.toByteArray().toUtf8()')
s = s.replace('.toByteArray().toUtf8()', '.toByteArray()')
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('bridge.cpp patched:', n)

# ---------- app/src/pages.cpp ----------
p = 'app/src/pages.cpp'
s = io.open(p, encoding='utf-8').read()
# 1. QProgressBar include
if '#include <QProgressBar>' not in s:
    s = s.replace('#include <QProgressBar>\n', '')  # clean potential dup
    s = s.replace('#include <QFileDialog>', '#include <QFileDialog>\n#include <QProgressBar>')
# 2. accent swatch lambda: colors has static storage; cannot be captured
s = s.replace('connect(b, &QPushButton::clicked, this, [i, colors, card]() {',
              'connect(b, &QPushButton::clicked, this, [this, i, card]() {')
s = s.replace('for (QPushButton *other : buttons)\n                    if (other->isCheckable() && other->width() == 26)\n                        other->setChecked(other == sender());',
              'const auto btns = card->findChildren<QPushButton *>();\n                for (QPushButton *other : btns)\n                    if (other->isCheckable() && other->width() == 26)\n                        other->setChecked(other == this->sender());')
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('pages.cpp patched')

# ---------- app/src/theme.cpp ----------
p = 'app/src/theme.cpp'
s = io.open(p, encoding='utf-8').read()
s = s.replace('QPixmap pm(size * qApp->devicePixelRatioF(), size * qApp->devicePixelRatioF());',
              'const qreal dpr = QGuiApplication::primaryScreen()\n                          ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;\n    QPixmap pm(int(size * dpr), int(size * dpr));')
s = s.replace('pm.setDevicePixelRatio(qApp->devicePixelRatioF());',
              'pm.setDevicePixelRatio(dpr);')
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('theme.cpp patched')
print('ROUND 2 DONE')
