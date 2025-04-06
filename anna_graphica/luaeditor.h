/* Based on code from Qt help.
 * Heavily modified and extended by Dmitry 'MatrixS_Master' Solovyev, 2024
 */

#ifndef LUAEDITOR_H
#define LUAEDITOR_H

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>

#define AG_LUAED_DEF_FONT 10
#define AG_LUAED_DEF_TABS 4
#define AG_LUAED_DEF_TAB "    "

class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    Highlighter(QTextDocument *parent = 0);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;
};

class LuaEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    LuaEditor(QWidget *parent = nullptr);
    virtual ~LuaEditor();

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth();
    void prependLines(QString prefix, bool add);

protected:
    void resizeEvent(QResizeEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);

private:
    QWidget *lineNumberArea;
    Highlighter* highlighter;

    bool CheckIndent();
    bool CheckUnIndent();
};

class LineNumberArea : public QWidget
{
public:
    LineNumberArea(LuaEditor *editor) : QWidget(editor), codeEditor(editor) {}

    QSize sizeHint() const override
    {
        return QSize(codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        codeEditor->lineNumberAreaPaintEvent(event);
    }

private:
    LuaEditor *codeEditor;
};

#endif // LUAEDITOR_H
