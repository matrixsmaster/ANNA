/* Based on code from Qt help. Modified and extended by Dmitry 'MatrixS_Master' Solovyev, 2024 */
#include <QPainter>
#include <QTextBlock>
#include "luaeditor.h"
#include "lua_tables.h"

LuaEditor::LuaEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    lineNumberArea = new LineNumberArea(this);

    connect(this, &LuaEditor::blockCountChanged, this, &LuaEditor::updateLineNumberAreaWidth);
    connect(this, &LuaEditor::updateRequest, this, &LuaEditor::updateLineNumberArea);
    //connect(this, &LuaEditor::cursorPositionChanged, this, &LuaEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    //highlightCurrentLine();

    QFont font;
    font.setFamily("monospace");
    font.setFixedPitch(true);
    font.setPointSize(10); // FIXME

    highlighter = new Highlighter(document());
}

LuaEditor::~LuaEditor()
{
    delete lineNumberArea;
    delete highlighter;
}

int LuaEditor::lineNumberAreaWidth()
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;

    return space;
}

void LuaEditor::updateLineNumberAreaWidth(int /* newBlockCount */)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void LuaEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void LuaEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void LuaEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        QColor lineColor = QColor(Qt::yellow).lighter(160);

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void LuaEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), Qt::lightGray);
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(Qt::black);
            painter.drawText(0, top, lineNumberArea->width(), fontMetrics().height(), Qt::AlignRight, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

Highlighter::Highlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;
    QTextCharFormat keywordFormat,typeFormat,quotationFormat,functionFormat,commentFormat;

    keywordFormat.setForeground(Qt::darkBlue);
    keywordFormat.setFontWeight(QFont::Bold);
    for (auto &&i : lua_keywords) {
        QString pat(i);
        pat = "\\b" + pat + "\\b";
        rule.pattern = QRegularExpression(pat);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    typeFormat.setForeground(Qt::darkGreen);
    typeFormat.setFontWeight(QFont::Bold);
    for (auto &&i : lua_types) {
        QString pat(i);
        pat = "\\b" + pat + "\\b";
        rule.pattern = QRegularExpression(pat);
        rule.format = typeFormat;
        highlightingRules.append(rule);
    }

    quotationFormat.setForeground(Qt::red);
    rule.pattern = QRegularExpression(QStringLiteral("\"[^\"]*\""));
    rule.format = quotationFormat;
    highlightingRules.append(rule);

    functionFormat.setFontWeight(QFont::Bold);
    rule.pattern = QRegularExpression(QStringLiteral("\\b[A-Za-z0-9_]+(?=\\()"));
    rule.format = functionFormat;
    highlightingRules.append(rule);

    commentFormat.setFontItalic(true);
    commentFormat.setForeground(Qt::blue);
    rule.pattern = QRegularExpression(QStringLiteral("--[^\n]*"));
    rule.format = commentFormat;
    highlightingRules.append(rule);
}

void Highlighter::highlightBlock(const QString &text)
{
    for (const HighlightingRule &rule : qAsConst(highlightingRules)) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

#if 0
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1)
        startIndex = text.indexOf(commentStartExpression);

    while (startIndex >= 0) {
        QRegularExpressionMatch match = commentEndExpression.match(text, startIndex);
        int endIndex = match.capturedStart();
        int commentLength = 0;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else
            commentLength = endIndex - startIndex + match.capturedLength();

        setFormat(startIndex, commentLength, multiLineCommentFormat);
        startIndex = text.indexOf(commentStartExpression, startIndex + commentLength);
    }
#endif
}
