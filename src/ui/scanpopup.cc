/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */
#include <QRegularExpression>
#include <QStringList>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBrowser>
#include <QTabWidget>
#include <QUrl>
#include <algorithm>
#include "scanpopup.hh"
#include "folding.hh"
#include "articlesaver.hh"
#include "utils.hh"
#include <QCursor>
#include <QPixmap>
#include <QMenu>
#include <QMouseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include "gestures.hh"

using std::set;
using std::map;
using std::pair;

namespace {

constexpr qsizetype smartLookupMaxChars = 300;
constexpr bool smartLookupAutoPinPopup  = true;

QString normalizeSmartLookupInput( QString text )
{
  text = text.normalized( QString::NormalizationForm_C );

  text.replace( QChar( 0x00A0 ), QLatin1Char( ' ' ) );
  text.replace( QRegularExpression( QStringLiteral( "[\\r\\n\\t]+" ) ), QStringLiteral( " " ) );
  text.replace( QRegularExpression( QStringLiteral( "\\s{2,}" ) ), QStringLiteral( " " ) );

  text = text.trimmed();

  if ( text.size() > smartLookupMaxChars ) {
    text = text.left( smartLookupMaxChars ).trimmed();
  }

  return text;
}

bool containsCjkText( const QString & text )
{
  for ( const QChar ch : text ) {
    const auto u = ch.unicode();
    if ( ( u >= 0x3400 && u <= 0x4DBF ) || ( u >= 0x4E00 && u <= 0x9FFF ) || ( u >= 0xF900 && u <= 0xFAFF ) ) {
      return true;
    }
  }

  return false;
}

QString compactCjkText( QString text )
{
  text.remove( QRegularExpression( QStringLiteral( "\\s+" ) ) );
  return text;
}

struct BuddhistGlossaryEntry
{
  QString term;
  QString hanViet;
  QString pinyin;
  QString meaningVi;
  QString category;
  QStringList suggestedTranslations;
  QStringList related;
};

QString smartLookupJsonPath()
{
  return QCoreApplication::applicationDirPath() + QStringLiteral( "/buddhist_terms.json" );
}

QString htmlEscape( const QString & text )
{
  return text.toHtmlEscaped();
}

QStringList jsonStringList( const QJsonObject & obj, const QString & key )
{
  QStringList values;
  const QJsonValue value = obj.value( key );

  if ( value.isString() ) {
    const QString text = normalizeSmartLookupInput( value.toString() );
    if ( !text.isEmpty() ) {
      values << text;
    }
  }
  else if ( value.isArray() ) {
    const QJsonArray array = value.toArray();
    for ( const QJsonValue & item : array ) {
      if ( item.isString() ) {
        const QString text = normalizeSmartLookupInput( item.toString() );
        if ( !text.isEmpty() && !values.contains( text ) ) {
          values << text;
        }
      }
    }
  }

  return values;
}

BuddhistGlossaryEntry glossaryEntryFromJsonValue( const QJsonValue & value )
{
  BuddhistGlossaryEntry entry;

  if ( value.isString() ) {
    entry.term = normalizeSmartLookupInput( value.toString() );
    return entry;
  }

  if ( !value.isObject() ) {
    return entry;
  }

  const QJsonObject obj = value.toObject();
  entry.term            = normalizeSmartLookupInput( obj.value( QStringLiteral( "term" ) ).toString() );
  entry.hanViet         = normalizeSmartLookupInput( obj.value( QStringLiteral( "han_viet" ) ).toString() );
  entry.pinyin          = normalizeSmartLookupInput( obj.value( QStringLiteral( "pinyin" ) ).toString() );
  entry.meaningVi       = normalizeSmartLookupInput( obj.value( QStringLiteral( "meaning_vi" ) ).toString() );
  entry.category        = normalizeSmartLookupInput( obj.value( QStringLiteral( "category" ) ).toString() );

  entry.suggestedTranslations = jsonStringList( obj, QStringLiteral( "suggested_translation" ) );
  if ( entry.suggestedTranslations.isEmpty() ) {
    entry.suggestedTranslations = jsonStringList( obj, QStringLiteral( "suggested_translations" ) );
  }

  entry.related = jsonStringList( obj, QStringLiteral( "related" ) );

  return entry;
}

QList< BuddhistGlossaryEntry > fallbackBuddhistGlossaryEntries()
{
  return {
    { QStringLiteral( "阿耨多羅三藐三菩提" ),
      QStringLiteral( "a-nậu-đa-la tam-miệu tam-bồ-đề" ),
      QString(),
      QStringLiteral( "Vô thượng Chánh đẳng Chánh giác" ),
      QStringLiteral( "Giác ngộ" ),
      {},
      {} },
    { QStringLiteral( "觀自在菩薩" ),
      QStringLiteral( "Quán Tự Tại Bồ Tát" ),
      QString(),
      QStringLiteral( "Danh hiệu Bồ-tát Quán Tự Tại" ),
      QStringLiteral( "Bát-nhã Tâm Kinh" ),
      {},
      { QStringLiteral( "菩薩" ), QStringLiteral( "般若波羅蜜多" ) } },
    { QStringLiteral( "般若波羅蜜多" ),
      QStringLiteral( "Bát-nhã Ba-la-mật-đa" ),
      QString(),
      QStringLiteral( "Trí tuệ đưa đến bờ giác" ),
      QStringLiteral( "Ba-la-mật" ),
      {},
      { QStringLiteral( "般若" ), QStringLiteral( "波羅蜜多" ) } },
    { QStringLiteral( "波羅蜜多" ),
      QStringLiteral( "Ba-la-mật-đa" ),
      QString(),
      QStringLiteral( "Đến bờ bên kia; sự viên mãn" ),
      QStringLiteral( "Ba-la-mật" ),
      {},
      {} },
    { QStringLiteral( "色即是空" ),
      QStringLiteral( "sắc tức thị không" ),
      QString(),
      QStringLiteral( "Sắc không khác không; hình tướng là không" ),
      QStringLiteral( "Bát-nhã Tâm Kinh" ),
      {},
      { QStringLiteral( "空即是色" ) } },
    { QStringLiteral( "空即是色" ),
      QStringLiteral( "không tức thị sắc" ),
      QString(),
      QStringLiteral( "Không không khác sắc; tánh không biểu hiện qua sắc" ),
      QStringLiteral( "Bát-nhã Tâm Kinh" ),
      {},
      { QStringLiteral( "色即是空" ) } },
    { QStringLiteral( "無明" ),
      QStringLiteral( "vô minh" ),
      QString(),
      QStringLiteral( "Không sáng suốt, không thấy rõ chân lý; một chi trong mười hai nhân duyên" ),
      QStringLiteral( "Thập nhị nhân duyên" ),
      {},
      { QStringLiteral( "十二因緣" ), QStringLiteral( "緣起" ) } },
    { QStringLiteral( "緣起" ),
      QStringLiteral( "duyên khởi" ),
      QString(),
      QStringLiteral( "Các pháp sinh khởi do nhân duyên" ),
      QStringLiteral( "Giáo lý căn bản" ),
      {},
      { QStringLiteral( "十二因緣" ), QStringLiteral( "無明" ) } },
    { QStringLiteral( "涅槃" ),
      QStringLiteral( "Niết-bàn" ),
      QString(),
      QStringLiteral( "Sự tịch diệt, giải thoát khỏi khổ đau và luân hồi" ),
      QStringLiteral( "Giải thoát" ),
      {},
      {} },
    { QStringLiteral( "菩薩" ),
      QStringLiteral( "Bồ-tát" ),
      QString(),
      QStringLiteral( "Bậc phát tâm giác ngộ, hành hạnh lợi mình lợi người" ),
      QStringLiteral( "Nhân vật/địa vị tu chứng" ),
      {},
      {} },
    { QStringLiteral( "般若" ),
      QStringLiteral( "Bát-nhã" ),
      QString(),
      QStringLiteral( "Trí tuệ thấy rõ tánh không" ),
      QStringLiteral( "Trí tuệ" ),
      {},
      { QStringLiteral( "般若波羅蜜多" ) } },
  };
}

QList< BuddhistGlossaryEntry > loadBuddhistGlossaryEntriesFromJson()
{
  QFile file( smartLookupJsonPath() );

  if ( !file.exists() ) {
    qDebug( "Buddhist glossary file was not found: %s", smartLookupJsonPath().toUtf8().constData() );
    return {};
  }

  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
    qWarning( "Could not open Buddhist glossary file: %s", smartLookupJsonPath().toUtf8().constData() );
    return {};
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
  if ( parseError.error != QJsonParseError::NoError ) {
    qWarning( "Could not parse Buddhist glossary file: %s", parseError.errorString().toUtf8().constData() );
    return {};
  }

  const QJsonArray array = doc.isArray() ? doc.array() : doc.object().value( QStringLiteral( "terms" ) ).toArray();

  QList< BuddhistGlossaryEntry > entries;
  QStringList seenTerms;
  for ( const QJsonValue & value : array ) {
    BuddhistGlossaryEntry entry = glossaryEntryFromJsonValue( value );
    if ( entry.term.isEmpty() || seenTerms.contains( entry.term ) ) {
      continue;
    }

    seenTerms << entry.term;
    entries << entry;
  }

  return entries;
}

const QList< BuddhistGlossaryEntry > & buddhistGlossaryEntries()
{
  static const QList< BuddhistGlossaryEntry > entries = [] {
    const QList< BuddhistGlossaryEntry > jsonEntries = loadBuddhistGlossaryEntriesFromJson();
    if ( !jsonEntries.isEmpty() ) {
      return jsonEntries;
    }

    return fallbackBuddhistGlossaryEntries();
  }();

  return entries;
}

QStringList fallbackBuddhistSeedTerms()
{
  return {
    QStringLiteral( "阿耨多羅三藐三菩提" ),
    QStringLiteral( "觀自在菩薩" ),
    QStringLiteral( "般若波羅蜜多" ),
    QStringLiteral( "波羅蜜多" ),
    QStringLiteral( "色即是空" ),
    QStringLiteral( "空即是色" ),
    QStringLiteral( "受想行識" ),
    QStringLiteral( "五蘊皆空" ),
    QStringLiteral( "照見五蘊" ),
    QStringLiteral( "舍利子" ),
    QStringLiteral( "諸法空相" ),
    QStringLiteral( "不生不滅" ),
    QStringLiteral( "不垢不淨" ),
    QStringLiteral( "不增不減" ),
    QStringLiteral( "十二因緣" ),
    QStringLiteral( "無明" ),
    QStringLiteral( "緣起" ),
    QStringLiteral( "涅槃" ),
    QStringLiteral( "菩提" ),
    QStringLiteral( "菩薩" ),
    QStringLiteral( "般若" ),
    QStringLiteral( "五蘊" ),
    QStringLiteral( "空相" ),
    QStringLiteral( "色" ),
    QStringLiteral( "受" ),
    QStringLiteral( "想" ),
    QStringLiteral( "行" ),
    QStringLiteral( "識" ),
  };
}

QStringList loadBuddhistSeedTermsFromJson()
{
  const QString path = smartLookupJsonPath();
  QFile file( path );

  if ( !file.exists() ) {
    qDebug( "Smart lookup terms file was not found: %s", path.toUtf8().constData() );
    return {};
  }

  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
    qWarning( "Could not open smart lookup terms file: %s", path.toUtf8().constData() );
    return {};
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
  if ( parseError.error != QJsonParseError::NoError ) {
    qWarning( "Could not parse smart lookup terms file: %s", parseError.errorString().toUtf8().constData() );
    return {};
  }

  const QJsonArray array = doc.isArray() ? doc.array() : doc.object().value( QStringLiteral( "terms" ) ).toArray();

  QStringList terms;
  for ( const QJsonValue & value : array ) {
    QString term;

    if ( value.isString() ) {
      term = value.toString();
    }
    else if ( value.isObject() ) {
      const QJsonObject obj = value.toObject();
      term                  = obj.value( QStringLiteral( "term" ) ).toString();
    }

    term = normalizeSmartLookupInput( term );
    if ( !term.isEmpty() && !terms.contains( term ) ) {
      terms << term;
    }
  }

  return terms;
}

QStringList buddhistSeedTerms()
{
  static const QStringList terms = [] {
    QStringList result;
    for ( const BuddhistGlossaryEntry & entry : buddhistGlossaryEntries() ) {
      if ( !entry.term.isEmpty() && !result.contains( entry.term ) ) {
        result << entry.term;
      }
    }

    if ( !result.isEmpty() ) {
      return result;
    }

    return fallbackBuddhistSeedTerms();
  }();

  return terms;
}

QStringList detectSmartLookupTerms( const QString & input )
{
  if ( !containsCjkText( input ) ) {
    return {};
  }

  const QString compactInput = compactCjkText( input );
  struct Match
  {
    int position;
    int length;
    QString term;
  };

  QList< Match > matches;
  for ( const QString & term : buddhistSeedTerms() ) {
    const int position = compactInput.indexOf( term );
    if ( position >= 0 ) {
      matches.push_back( Match{ position, static_cast< int >( term.size() ), term } );
    }
  }

  std::sort( matches.begin(), matches.end(), []( const Match & lhs, const Match & rhs ) {
    if ( lhs.position != rhs.position ) {
      return lhs.position < rhs.position;
    }
    return lhs.length > rhs.length;
  } );

  QStringList terms;
  for ( const Match & match : matches ) {
    if ( !terms.contains( match.term ) ) {
      terms << match.term;
    }
  }

  return terms;
}

QString chooseSmartLookupQuery( const QString & input, const QStringList & detectedTerms )
{
  if ( detectedTerms.isEmpty() ) {
    return input;
  }

  const QString compactInput = compactCjkText( input );
  if ( compactInput == detectedTerms.first() ) {
    return input;
  }

  // If the clipboard contains a whole sutra sentence, use the first detected
  // Buddhist term as the dictionary lookup query. The remaining terms are shown
  // in the popup status bar as hints.
  if ( compactInput.size() > detectedTerms.first().size() + 2 || detectedTerms.size() > 1 ) {
    return detectedTerms.first();
  }

  return input;
}
QList< BuddhistGlossaryEntry > glossaryEntriesForTerms( const QString & primaryTerm, const QStringList & detectedTerms )
{
  QStringList wantedTerms = detectedTerms;
  if ( !primaryTerm.isEmpty() && !wantedTerms.contains( primaryTerm ) ) {
    wantedTerms.prepend( primaryTerm );
  }

  QList< BuddhistGlossaryEntry > result;
  for ( const QString & wantedTerm : wantedTerms ) {
    for ( const BuddhistGlossaryEntry & entry : buddhistGlossaryEntries() ) {
      if ( entry.term == wantedTerm ) {
        result << entry;
        break;
      }
    }
  }

  return result;
}

QString glossaryEntryHtml( const BuddhistGlossaryEntry & entry )
{
  QString html;
  html += QStringLiteral( "<div style='margin:0 0 14px 0; padding:10px; border:1px solid #ddd; border-radius:8px;'>" );
  html += QStringLiteral( "<div style='font-size:22px; font-weight:700; margin-bottom:6px;'>%1</div>" )
            .arg( htmlEscape( entry.term ) );

  if ( !entry.hanViet.isEmpty() ) {
    html += QStringLiteral( "<div><b>Hán Việt:</b> %1</div>" ).arg( htmlEscape( entry.hanViet ) );
  }
  if ( !entry.pinyin.isEmpty() ) {
    html += QStringLiteral( "<div><b>Pinyin:</b> %1</div>" ).arg( htmlEscape( entry.pinyin ) );
  }
  if ( !entry.meaningVi.isEmpty() ) {
    html += QStringLiteral( "<div><b>Nghĩa:</b> %1</div>" ).arg( htmlEscape( entry.meaningVi ) );
  }
  if ( !entry.category.isEmpty() ) {
    html += QStringLiteral( "<div><b>Nhóm:</b> %1</div>" ).arg( htmlEscape( entry.category ) );
  }
  if ( !entry.suggestedTranslations.isEmpty() ) {
    html += QStringLiteral( "<div><b>Gợi ý dịch:</b> %1</div>" )
              .arg( htmlEscape( entry.suggestedTranslations.join( QStringLiteral( " / " ) ) ) );
  }
  if ( !entry.related.isEmpty() ) {
    html += QStringLiteral( "<div><b>Liên quan:</b> %1</div>" )
              .arg( htmlEscape( entry.related.join( QStringLiteral( "、" ) ) ) );
  }

  html += QStringLiteral( "</div>" );
  return html;
}

QString glossaryHtml( const QString & primaryTerm, const QStringList & detectedTerms )
{
  const QList< BuddhistGlossaryEntry > entries = glossaryEntriesForTerms( primaryTerm, detectedTerms );

  QString html;
  html += QStringLiteral( "<html><head><meta charset='utf-8'></head>" );
  html += QStringLiteral(
    "<body style='font-family:&quot;Segoe UI&quot;, Arial, sans-serif; font-size:14px; margin:12px;'>" );
  html += QStringLiteral( "<h2 style='margin-top:0;'>Phật học / Buddhist Glossary</h2>" );

  if ( entries.isEmpty() ) {
    html += QStringLiteral( "<p>Không có glossary cho thuật ngữ này.</p>" );
  }
  else {
    for ( const BuddhistGlossaryEntry & entry : entries ) {
      html += glossaryEntryHtml( entry );
    }
  }

  html += QStringLiteral( "<p style='color:#777; font-size:12px;'>Nguồn dữ liệu: buddhist_terms.json</p>" );
  html += QStringLiteral( "</body></html>" );
  return html;
}

QTextBrowser * findBuddhistGlossaryBrowser( QTabWidget * tabs )
{
  if ( !tabs ) {
    return nullptr;
  }

  for ( int i = 0; i < tabs->count(); ++i ) {
    QTextBrowser * browser = qobject_cast< QTextBrowser * >( tabs->widget( i ) );
    if ( browser && browser->objectName() == QStringLiteral( "buddhistGlossaryBrowser" ) ) {
      return browser;
    }
  }

  return nullptr;
}

void removeBuddhistGlossaryTab( QTabWidget * tabs )
{
  if ( !tabs ) {
    return;
  }

  for ( int i = 0; i < tabs->count(); ++i ) {
    QTextBrowser * browser = qobject_cast< QTextBrowser * >( tabs->widget( i ) );
    if ( browser && browser->objectName() == QStringLiteral( "buddhistGlossaryBrowser" ) ) {
      tabs->removeTab( i );
      browser->deleteLater();
      return;
    }
  }
}

void updateBuddhistGlossaryTab( QTabWidget * tabs, const QString & primaryTerm, const QStringList & detectedTerms )
{
  if ( !tabs ) {
    return;
  }

  const QList< BuddhistGlossaryEntry > entries = glossaryEntriesForTerms( primaryTerm, detectedTerms );
  if ( entries.isEmpty() ) {
    removeBuddhistGlossaryTab( tabs );
    return;
  }

  QTextBrowser * browser = findBuddhistGlossaryBrowser( tabs );
  if ( !browser ) {
    browser = new QTextBrowser( tabs );
    browser->setObjectName( QStringLiteral( "buddhistGlossaryBrowser" ) );
    browser->setOpenExternalLinks( true );
    tabs->addTab( browser, QStringLiteral( "Phật học" ) );
  }

  browser->setHtml( glossaryHtml( primaryTerm, detectedTerms ) );
}

QString webReferenceUrlEncode( const QString & text )
{
  return QString::fromLatin1( QUrl::toPercentEncoding( text ) );
}

QStringList webReferenceTerms( const QString & primaryTerm, const QStringList & detectedTerms )
{
  QStringList terms;

  const QString primary = normalizeSmartLookupInput( primaryTerm );
  if ( !primary.isEmpty() ) {
    terms << primary;
  }

  for ( const QString & term : detectedTerms ) {
    const QString normalized = normalizeSmartLookupInput( term );
    if ( !normalized.isEmpty() && !terms.contains( normalized ) ) {
      terms << normalized;
    }
  }

  constexpr int maxWebReferenceTerms = 6;
  while ( terms.size() > maxWebReferenceTerms ) {
    terms.removeLast();
  }

  return terms;
}

QString webReferenceLinkHtml( const QString & label, const QString & url )
{
  return QStringLiteral( "<li><a href='%1'>%2</a></li>" ).arg( htmlEscape( url ), htmlEscape( label ) );
}

QString webReferenceHtml( const QString & primaryTerm, const QStringList & detectedTerms )
{
  const QStringList terms = webReferenceTerms( primaryTerm, detectedTerms );

  QString html;
  html += QStringLiteral( "<html><head><meta charset='utf-8'></head>" );
  html += QStringLiteral(
    "<body style='font-family:&quot;Segoe UI&quot;, Arial, sans-serif; font-size:14px; margin:12px;'>" );
  html += QStringLiteral( "<h2 style='margin-top:0;'>Wikipedia / Web Reference</h2>" );

  if ( terms.isEmpty() ) {
    html += QStringLiteral( "<p>Không có thuật ngữ để mở nguồn tham khảo.</p>" );
  }
  else {
    html +=
      QStringLiteral( "<p style='color:#555;'>Các liên kết này mở trong trình duyệt ngoài để tham khảo nhanh.</p>" );

    for ( const QString & term : terms ) {
      const QString encoded = webReferenceUrlEncode( term );
      html +=
        QStringLiteral( "<div style='border:1px solid #ddd; border-radius:8px; padding:10px; margin:0 0 10px 0;'>" );
      html += QStringLiteral( "<h3 style='margin:0 0 8px 0;'>%1</h3>" ).arg( htmlEscape( term ) );
      html += QStringLiteral( "<ul style='margin-top:6px;'>" );
      html += webReferenceLinkHtml( QStringLiteral( "Wikipedia tiếng Trung" ),
                                    QStringLiteral( "https://zh.wikipedia.org/wiki/%1" ).arg( encoded ) );
      html += webReferenceLinkHtml(
        QStringLiteral( "Wikipedia search tiếng Anh" ),
        QStringLiteral( "https://en.wikipedia.org/wiki/Special:Search?search=%1" ).arg( encoded ) );
      html += webReferenceLinkHtml( QStringLiteral( "Wiktionary" ),
                                    QStringLiteral( "https://en.wiktionary.org/wiki/%1" ).arg( encoded ) );
      html += webReferenceLinkHtml( QStringLiteral( "Google Search" ),
                                    QStringLiteral( "https://www.google.com/search?q=%1" ).arg( encoded ) );
      html += QStringLiteral( "</ul>" );
      html += QStringLiteral( "</div>" );
    }
  }

  html += QStringLiteral(
    "<p style='color:#777; font-size:12px;'>Gợi ý: dùng các nguồn web như tài liệu tham khảo, không thay thế glossary nội bộ.</p>" );
  html += QStringLiteral( "</body></html>" );
  return html;
}

QTextBrowser * findWebReferenceBrowser( QTabWidget * tabs )
{
  if ( !tabs ) {
    return nullptr;
  }

  for ( int i = 0; i < tabs->count(); ++i ) {
    QTextBrowser * browser = qobject_cast< QTextBrowser * >( tabs->widget( i ) );
    if ( browser && browser->objectName() == QStringLiteral( "webReferenceBrowser" ) ) {
      return browser;
    }
  }

  return nullptr;
}

void removeWebReferenceTab( QTabWidget * tabs )
{
  if ( !tabs ) {
    return;
  }

  for ( int i = 0; i < tabs->count(); ++i ) {
    QTextBrowser * browser = qobject_cast< QTextBrowser * >( tabs->widget( i ) );
    if ( browser && browser->objectName() == QStringLiteral( "webReferenceBrowser" ) ) {
      tabs->removeTab( i );
      browser->deleteLater();
      return;
    }
  }
}

void updateWebReferenceTab( QTabWidget * tabs, const QString & primaryTerm, const QStringList & detectedTerms )
{
  if ( !tabs ) {
    return;
  }

  const QStringList terms = webReferenceTerms( primaryTerm, detectedTerms );
  if ( terms.isEmpty() ) {
    removeWebReferenceTab( tabs );
    return;
  }

  QTextBrowser * browser = findWebReferenceBrowser( tabs );
  if ( !browser ) {
    browser = new QTextBrowser( tabs );
    browser->setObjectName( QStringLiteral( "webReferenceBrowser" ) );
    browser->setOpenExternalLinks( true );
    tabs->addTab( browser, QStringLiteral( "Web" ) );
  }

  browser->setHtml( webReferenceHtml( primaryTerm, detectedTerms ) );
}

} // namespace

#ifdef Q_OS_MAC
  #include "macos/macmouseover.hh"
  #define MouseOver MacMouseOver
#endif

static const Qt::WindowFlags defaultUnpinnedWindowFlags = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;

static const Qt::WindowFlags pinnedWindowFlags = Qt::Window;

ScanPopup::ScanPopup( QWidget * parent,
                      Config::Class & cfg_,
                      ArticleNetworkAccessManager & articleNetMgr,
                      History & history_ ):
  QMainWindow( parent ),
  cfg( cfg_ ),
  allDictionaries( *GlobalBroadcaster::instance()->getAllDictionaries() ),
  groups( *GlobalBroadcaster::instance()->getGroups() ),
  history( history_ ),
  escapeAction( this ),
  switchExpandModeAction( this ),
  focusTranslateLineAction( this ),
  stopAudioAction( this ),
  openSearchAction( this ),
  wordFinder( this ),
  dictionaryBar( this, cfg.preferences.maxDictionaryRefsInContextMenu ),
  articleNetMgr( articleNetMgr ),
  hideTimer( this )
{
  QWidget * toolBarWidget = new QWidget( this );
  ui.setupUi( toolBarWidget );

  QToolBar * toolBar = new QToolBar( "Tool bar", this );
  toolBar->setObjectName( "popupToolBar" );
  toolBar->addWidget( toolBarWidget );

  groupList    = new GroupComboBox( this );
  translateBox = new TranslateBox( this );

  groupList->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Expanding );
  translateBox->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

  QToolBar * searchBar = new QToolBar( "Search bar", this );
  searchBar->setObjectName( "popupSearchBar" );
  groupListAction = searchBar->addWidget( groupList );
  searchBar->addWidget( translateBox );
  searchBar->toggleViewAction()->setEnabled( false );

  foundBar = new QToolBar( "Navigation bar", this );
  foundBar->setObjectName( "popupNavigationBar" );
  foundBar->setAllowedAreas( Qt::LeftToolBarArea | Qt::RightToolBarArea );

  searchBar->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Expanding );
  searchBar->setMovable( false );
  toolBar->setFloatable( false );
  dictionaryBar.setFloatable( false );
  foundBar->setFloatable( false );

  searchBar->setContentsMargins( 0, 0, 2, 0 );
  toolBar->setContentsMargins( 0, 0, 0, 0 );

  addToolBar( Qt::TopToolBarArea, searchBar );
  addToolBar( Qt::TopToolBarArea, toolBar );
  addToolBarBreak();
  addToolBar( Qt::TopToolBarArea, &dictionaryBar );
  addToolBar( Qt::RightToolBarArea, foundBar );

  if ( layoutDirection() == Qt::RightToLeft ) {
    ui.goBackButton->setIcon( QIcon( ":/icons/next.svg" ) );
    ui.goForwardButton->setIcon( QIcon( ":/icons/previous.svg" ) );
  }

  setStatusBar( nullptr );

  mainStatusBar = new MainStatusBar( this );

  tabWidget = new MainTabWidget( this );
  tabWidget->setTabsClosable( true );
  tabWidget->setHideSingleTab( true );
  tabWidget->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Expanding );
  connect( tabWidget, &QTabWidget::tabCloseRequested, this, [ this ]( int index ) {
    if ( index > 0 ) {
      auto widget = tabWidget->widget( index );
      tabWidget->removeTab( index );
      widget->deleteLater();
    }
  } );

  definition = new ArticleView( tabWidget,
                                articleNetMgr,
                                true,
                                cfg,
                                translateBox->translateLine(),
                                dictionaryBar.toggleViewAction(),
                                cfg.lastPopupGroupId );

  tabWidget->addTab( definition, tr( "Definition" ) );
  tabWidget->tabBar()->setTabButton( 0, QTabBar::RightSide, nullptr );
  tabWidget->tabBar()->setTabButton( 0, QTabBar::LeftSide, nullptr );

  setCentralWidget( tabWidget );

  resize( 247, 400 );

  QScreen * screen = QGuiApplication::primaryScreen();
  if ( screen ) {
    int maxWidth = screen->availableGeometry().width() * 0.8;
    setMaximumWidth( maxWidth );
  }

  translateBox->setMaximumWidth( 200 );
  groupList->setMaximumWidth( 200 );

  connect( definition, &ArticleView::inspectSignal, this, &ScanPopup::inspectElementWhenPinned );
  connect( definition, &ArticleView::forceAddWordToHistory, this, &ScanPopup::forceAddWordToHistory );
  connect( this, &ScanPopup::closeMenu, definition, &ArticleView::closePopupMenu );
  connect( definition, &ArticleView::sendWordToHistory, this, &ScanPopup::sendWordToHistory );
  connect( definition, &ArticleView::typingEvent, this, &ScanPopup::typingEvent );
  connect( definition, &ArticleView::updateFoundInDictsList, this, &ScanPopup::updateFoundInDictsList );
  connect( &dictionaryBar, &DictionaryBar::visibilityChanged, this, &ScanPopup::dictionaryBar_visibility_changed );

  connect( ui.goBackButton, &QToolButton::pressed, this, &ScanPopup::goBackButton_clicked );
  connect( ui.goForwardButton, &QToolButton::pressed, this, &ScanPopup::goForwardButton_clicked );
  connect( ui.pronounceButton, &QToolButton::pressed, this, &ScanPopup::pronounceButton_clicked );
  connect( ui.saveArticleButton, &QToolButton::pressed, this, &ScanPopup::saveArticleButton_clicked );
  connect( ui.sendWordButton, &QToolButton::pressed, this, &ScanPopup::sendWordButton_clicked );
  connect( ui.sendWordToFavoritesButton, &QToolButton::pressed, this, &ScanPopup::sendWordToFavoritesButton_clicked );

  openSearchAction.setShortcut( QKeySequence( "Ctrl+F" ) );
  openSearchAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );
  addAction( &openSearchAction );
  connect( &openSearchAction, &QAction::triggered, definition, &ArticleView::openSearch );

  wordListDefaultFont      = translateBox->completerWidget()->font();
  translateLineDefaultFont = translateBox->font();
  groupListDefaultFont     = groupList->font();

  translateBox->translateLine()->installEventFilter( this );
  definition->installEventFilter( this );
  this->installEventFilter( this );

  connect( translateBox->translateLine(), &QLineEdit::textEdited, this, &ScanPopup::translateInputChanged );

  connect( translateBox, &TranslateBox::returnPressed, this, &ScanPopup::translateInputFinished );

  ui.pronounceButton->setDisabled( true );

  groupList->fill( groups );
  groupList->setCurrentGroup( cfg.lastPopupGroupId );

  definition->setCurrentGroupId( groupList->getCurrentGroup() );
  definition->setSelectionBySingleClick( cfg.preferences.selectWordBySingleClick );

  const Instances::Group * igrp = groups.findGroup( cfg.lastPopupGroupId );
  if ( cfg.lastPopupGroupId == GroupId::AllGroupId ) {
    if ( igrp ) {
      igrp->checkMutedDictionaries( &cfg.popupMutedDictionaries );
    }
    dictionaryBar.setMutedDictionaries( &cfg.popupMutedDictionaries );
  }
  else {
    Config::Group * grp = cfg.getGroup( cfg.lastPopupGroupId );
    if ( igrp && grp ) {
      igrp->checkMutedDictionaries( &grp->popupMutedDictionaries );
    }
    dictionaryBar.setMutedDictionaries( grp ? &grp->popupMutedDictionaries : nullptr );
  }


  connect( &dictionaryBar, &DictionaryBar::editGroupRequested, this, &ScanPopup::editGroupRequested );
  connect( this, &ScanPopup::closeMenu, &dictionaryBar, &DictionaryBar::closePopupMenu );
  connect( &dictionaryBar, &DictionaryBar::showDictionaryInfo, this, &ScanPopup::showDictionaryInfo );
  connect( &dictionaryBar, &DictionaryBar::openDictionaryFolder, this, &ScanPopup::openDictionaryFolder );
  // Connect the dictionary bar's status bar message signal to the scan popup's status bar message slot
  // Use lambda to adapt the signal (2 parameters) to the slot (3 parameters with default)
  connect( &dictionaryBar,
           &DictionaryBar::showStatusBarMessage,
           this,
           [ this ]( const QString & message, int timeout ) {
             showStatusBarMessage( message, timeout );
           } );

  connect( &GlobalBroadcaster::instance()->pronounce_engine,
           &PronounceEngine::emitAudio,
           this,
           [ this ]( auto audioUrl ) {
             definition->setAudioLink( audioUrl );
             if ( !isActiveWindow() ) {
               return;
             }
             if ( cfg.preferences.pronounceOnLoadPopup ) {
               // Use a small delay to avoid audio clipping on Windows during window activation/rendering
               QTimer::singleShot( 150, definition, [ this, audioUrl ]() {
                 definition->playAudio( QUrl::fromEncoded( audioUrl.toUtf8() ) );
               } );
             }
           } );
  pinnedGeometry = cfg.popupWindowGeometry;
  if ( cfg.popupWindowGeometry.size() ) {
    restoreGeometry( cfg.popupWindowGeometry );
  }

  if ( cfg.popupWindowState.size() ) {
    restoreState( cfg.popupWindowState );
  }


  ui.onTopButton->setChecked( cfg.popupWindowAlwaysOnTop );
  ui.onTopButton->setVisible( cfg.pinPopupWindow );
  connect( ui.onTopButton, &QAbstractButton::clicked, this, &ScanPopup::alwaysOnTopClicked );

  ui.pinButton->setChecked( cfg.pinPopupWindow );

  if ( cfg.pinPopupWindow ) {
    Qt::WindowFlags flags = pinnedWindowFlags;
    if ( cfg.popupWindowAlwaysOnTop ) {
      flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags( flags );
#ifdef Q_OS_MACOS
    setAttribute( Qt::WA_MacAlwaysShowToolWindow );
#endif
  }
  else {
    setWindowFlags( unpinnedWindowFlags() );
#ifdef Q_OS_MACOS
    setAttribute( Qt::WA_MacAlwaysShowToolWindow, false );
#endif
  }

  connect( GlobalBroadcaster::instance(),
           &GlobalBroadcaster::mutedDictionariesChanged,
           this,
           &ScanPopup::mutedDictionariesChanged );

  definition->focus();

  escapeAction.setShortcut( QKeySequence( "Esc" ) );
  addAction( &escapeAction );
  connect( &escapeAction, &QAction::triggered, this, &ScanPopup::escapePressed );

  focusTranslateLineAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );
  addAction( &focusTranslateLineAction );
  focusTranslateLineAction.setShortcuts( QList< QKeySequence >()
                                         << QKeySequence( "Alt+D" ) << QKeySequence( "Ctrl+L" ) );

  connect( &focusTranslateLineAction, &QAction::triggered, this, &ScanPopup::focusTranslateLine );

  stopAudioAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );
  addAction( &stopAudioAction );
  stopAudioAction.setShortcut( QKeySequence( "Ctrl+Shift+S" ) );

  connect( &stopAudioAction, &QAction::triggered, this, &ScanPopup::stopAudio );

  QAction * const focusArticleViewAction = new QAction( this );
  focusArticleViewAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  focusArticleViewAction->setShortcut( QKeySequence( "Ctrl+N" ) );
  addAction( focusArticleViewAction );
  connect( focusArticleViewAction, &QAction::triggered, definition, &ArticleView::focus );

  switchExpandModeAction.setShortcuts( QList< QKeySequence >() << QKeySequence( Qt::CTRL | Qt::Key_8 )
                                                               << QKeySequence( Qt::CTRL | Qt::Key_Asterisk )
                                                               << QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_8 ) );

  addAction( &switchExpandModeAction );
  connect( &switchExpandModeAction, &QAction::triggered, this, &ScanPopup::switchExpandOptionalPartsMode );

  connect( groupList, &QComboBox::currentIndexChanged, this, &ScanPopup::currentGroupChanged );

  connect( &wordFinder, &WordFinder::finished, this, &ScanPopup::prefixMatchFinished );

  connect( ui.pinButton, &QAbstractButton::clicked, this, &ScanPopup::pinButtonClicked );

  connect( definition, &ArticleView::pageLoaded, this, &ScanPopup::pageLoaded );

  connect( definition, &ArticleView::statusBarMessage, this, &ScanPopup::showStatusBarMessage );

  connect( definition, &ArticleView::titleChanged, this, &ScanPopup::titleChanged );
  connect( definition, &ArticleView::activeArticleChanged, this, &ScanPopup::activeArticleChanged );
  connect( definition, &ArticleView::sendWordToInputLine, this, &ScanPopup::translateWord );

  connect( GlobalBroadcaster::instance(),
           &GlobalBroadcaster::websiteDictionarySignal,
           this,
           &ScanPopup::openWebsiteInNewTab );

  connect( tabWidget, &QTabWidget::currentChanged, this, [ this ]( int ) {
    updateBackForwardButtons();
  } );

#ifdef Q_OS_MAC
  connect( &MouseOver::instance(), &MouseOver::hovered, this, &ScanPopup::handleInputWord );
#endif

  hideTimer.setSingleShot( true );
  hideTimer.setInterval( 400 );

  connect( &hideTimer, &QTimer::timeout, this, &ScanPopup::hideTimerExpired );

  mouseGrabPollTimer.setSingleShot( false );
  mouseGrabPollTimer.setInterval( 10 );
  connect( &mouseGrabPollTimer, &QTimer::timeout, this, &ScanPopup::mouseGrabPoll );
#ifdef Q_OS_MAC
  MouseOver::instance().setPreferencesPtr( &( cfg.preferences ) );
#endif
  ui.goBackButton->setEnabled( false );
  ui.goForwardButton->setEnabled( false );

#ifndef Q_OS_MACOS
  grabGesture( Gestures::GDPinchGestureType );
  grabGesture( Gestures::GDSwipeGestureType );
#endif

#ifdef WITH_X11
  if ( QGuiApplication::platformName() == "xcb" ) {
    scanFlag = new ScanFlag( this );

    connect( scanFlag, &ScanFlag::requestScanPopup, this, [ this ] {
      translateWordFromSelection();
    } );

    // Use delay show to prevent popup from showing up while selection is still in progress
    // Only certain software has this problem (e.g. Chrome)
    selectionDelayTimer.setSingleShot( true );
    selectionDelayTimer.setInterval( cfg.preferences.selectionChangeDelayTimer );

    connect( &selectionDelayTimer, &QTimer::timeout, this, &ScanPopup::translateWordFromSelection );
  }
#endif
  applyZoomFactor();
}

void ScanPopup::onActionTriggered()
{
  QAction * action = qobject_cast< QAction * >( sender() );
  if ( action != nullptr ) {
    auto dictId = action->data().toString();
    qDebug() << "Action triggered:" << dictId;

    if ( auto otherView = findArticleViewByDictId( dictId ) ) {
      tabWidget->setCurrentWidget( otherView );
      return;
    }

    tabWidget->setCurrentWidget( definition );

    definition->jumpToDictionary( dictId, true );
  }
}

void ScanPopup::updateFoundInDictsList()
{
  if ( !foundBar->isVisible() ) {
    // nothing to do, the list is not visible
    return;
  }
  foundBar->setUpdatesEnabled( false );

  unsigned currentId           = groupList->getCurrentGroup();
  const Instances::Group * grp = groups.findGroup( currentId );

  auto dictionaries = grp ? grp->dictionaries : allDictionaries;
  QStringList ids   = definition->getArticlesList();
  QString activeId  = definition->getActiveArticleId();
  foundBar->clear();
  if ( actionGroup != nullptr ) {
    actionGroup->deleteLater();
  }
  actionGroup = new QActionGroup( this );
  actionGroup->setExclusive( true );
  for ( QStringList::const_iterator i = ids.constBegin(); i != ids.constEnd(); ++i ) {
    // Find this dictionary

    for ( unsigned x = dictionaries.size(); x--; ) {
      if ( dictionaries[ x ]->getId() == i->toUtf8().data() ) {

        auto dictionary  = dictionaries[ x ];
        QIcon icon       = dictionary->getIcon();
        QString dictName = QString::fromUtf8( dictionary->getName().c_str() );
        QAction * action = new QAction( dictName, this );
        action->setIcon( icon );
        QString id = QString::fromStdString( dictionary->getId() );
        action->setData( id );
        action->setCheckable( true );
        if ( id == activeId ) {
          action->setChecked( true );
        }
        connect( action, &QAction::triggered, this, &ScanPopup::onActionTriggered );
        foundBar->addAction( action );
        actionGroup->addAction( action );
        break;
      }
    }
  }

  foundBar->setUpdatesEnabled( true );
}

void ScanPopup::reloadAllTabs()
{
  for ( int i = 0; i < tabWidget->count(); ++i ) {
    if ( auto view = qobject_cast< ArticleView * >( tabWidget->widget( i ) ) ) {
      view->reload();
    }
  }
}

void ScanPopup::refresh()
{
  // currentIndexChanged() signal is very trigger-happy. To avoid triggering
  // it, we disconnect it while we're clearing and filling back groups.
  disconnect( groupList, &GroupComboBox::currentIndexChanged, this, &ScanPopup::currentGroupChanged );

  auto OldGroupID = groupList->getCurrentGroup();

  // repopulate
  groupList->clear();
  groupList->fill( groups );

  groupList->setCurrentGroup( OldGroupID ); // This does nothing if OldGroupID doesn't exist;

  groupListAction->setVisible( !cfg.groups.empty() );

  dictionaryBar.updateToGroup( groups.findGroup( groupList->getCurrentGroup() ),
                               &cfg.popupMutedDictionaries,
                               cfg,
                               true );
  setDictionaryIconSize();

  definition->syncBackgroundColorWithCfgDarkReader();

  connect( groupList, &GroupComboBox::currentIndexChanged, this, &ScanPopup::currentGroupChanged );
#ifdef WITH_X11
  if ( scanFlag ) {
    selectionDelayTimer.setInterval( cfg.preferences.selectionChangeDelayTimer );
  }
#endif
}


ScanPopup::~ScanPopup()
{
  saveConfigData();
#ifndef Q_OS_MACOS
  ungrabGesture( Gestures::GDPinchGestureType );
  ungrabGesture( Gestures::GDSwipeGestureType );
#endif
}

void ScanPopup::saveConfigData() const
{
  // Save state, geometry and pin status
  cfg.popupWindowState       = saveState();
  cfg.popupWindowGeometry    = saveGeometry();
  cfg.pinPopupWindow         = ui.pinButton->isChecked();
  cfg.popupWindowAlwaysOnTop = ui.onTopButton->isChecked();
}

void ScanPopup::inspectElementWhenPinned( QWebEnginePage * page )
{
  if ( cfg.pinPopupWindow ) {
    emit inspectSignal( page );
  }
}

void ScanPopup::applyZoomFactor() const
{
  for ( int i = 0; i < tabWidget->count(); ++i ) {
    if ( auto view = qobject_cast< ArticleView * >( tabWidget->widget( i ) ) ) {
      view->setZoomFactor( cfg.preferences.zoomFactor );
    }
  }
}

Qt::WindowFlags ScanPopup::unpinnedWindowFlags() const
{
  return defaultUnpinnedWindowFlags;
}

void ScanPopup::translateWordFromPrimaryClipboard()
{
  translateWordFromClipboard( QClipboard::Clipboard );
}

void ScanPopup::translateWordFromSelection()
{
  translateWordFromClipboard( QClipboard::Selection );
}

void ScanPopup::editGroupRequested()
{
  emit editGroupRequest( groupList->getCurrentGroup() );
}

void ScanPopup::translateWordFromClipboard( QClipboard::Mode m )
{
  GlobalBroadcaster::instance()->is_popup = true;

  if ( m == QClipboard::Selection && Utils::isWayland() ) {
    return;
  }

  QClipboard * clipboard = QApplication::clipboard();
  if ( !clipboard ) {
    qWarning( "Clipboard is not available" );
    return;
  }

  QString subtype = QStringLiteral( "plain" );
  QString str     = normalizeSmartLookupInput( clipboard->text( subtype, m ) );

  if ( str.isEmpty() ) {
    return;
  }

  qDebug( "Translate from clipboard %d -> %s", qToUnderlying( m ), str.toStdString().c_str() );

  translateWord( str );
}

void ScanPopup::translateWord( const QString & word )
{
  const QString normalizedWord = normalizeSmartLookupInput( cfg.preferences.sanitizeInputPhrase( word ) );
  const QStringList smartTerms = detectSmartLookupTerms( normalizedWord );

  pendingWord = chooseSmartLookupQuery( normalizedWord, smartTerms );

  if ( pendingWord.isEmpty() ) {
    return; // Nothing there
  }

#ifdef WITH_X11
  emit hideScanFlag();
#endif

  engagePopup( false, true );
  updateBuddhistGlossaryTab( tabWidget, pendingWord, smartTerms );
  updateWebReferenceTab( tabWidget, pendingWord, smartTerms );

  if ( !smartTerms.isEmpty() && pendingWord != normalizedWord ) {
    showStatusBarMessage( tr( "Smart terms: %1" ).arg( smartTerms.join( QStringLiteral( " | " ) ) ), 8000 );
  }
}

#ifdef WITH_X11
void ScanPopup::showEngagePopup()
{
  engagePopup( false );
}
#endif

[[deprecated]] void ScanPopup::handleInputWord( const QString & str, bool forcePopup )
{
  const QString sanitizedPhrase = normalizeSmartLookupInput( cfg.preferences.sanitizeInputPhrase( str ) );
  const QStringList smartTerms  = detectSmartLookupTerms( sanitizedPhrase );
  const QString smartQuery      = chooseSmartLookupQuery( sanitizedPhrase, smartTerms );

  if ( smartQuery.isEmpty() ) {
    return;
  }

  if ( isVisible() && smartQuery == pendingWord ) {
    // Attempt to translate the same word we already have shown in popup.
    // Ignore it, as it is probably a spurious mouseover event.
    return;
  }

  pendingWord = smartQuery;

#ifdef WITH_X11
  if ( cfg.preferences.showScanFlag ) {
    emit showScanFlag();
    return;
  }
#endif

  engagePopup( forcePopup );
  updateBuddhistGlossaryTab( tabWidget, pendingWord, smartTerms );
  updateWebReferenceTab( tabWidget, pendingWord, smartTerms );

  if ( !smartTerms.isEmpty() && pendingWord != sanitizedPhrase ) {
    showStatusBarMessage( tr( "Smart terms: %1" ).arg( smartTerms.join( QStringLiteral( " | " ) ) ), 8000 );
  }
}

void ScanPopup::engagePopup( bool forcePopup, bool giveFocus )
{
  if ( smartLookupAutoPinPopup && !ui.pinButton->isChecked() ) {
    uninterceptMouse();

    ui.pinButton->setChecked( true );
    ui.onTopButton->setVisible( true );

    Qt::WindowFlags flags = pinnedWindowFlags;
    if ( ui.onTopButton->isChecked() ) {
      flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags( flags );

#ifdef Q_OS_MACOS
    setAttribute( Qt::WA_MacAlwaysShowToolWindow );
#endif

    hideTimer.stop();
    cfg.pinPopupWindow = true;
  }

  if ( cfg.preferences.scanToMainWindow && !forcePopup ) {
    // Send translated word to main window istead of show popup
    emit sendPhraseToMainWindow( pendingWord );
    return;
  }

  if ( !isVisible() ) {
    // Need to show the window

    if ( !ui.pinButton->isChecked() ) {
      // Decide where should the window land

      QPoint currentPos = QCursor::pos();

      auto screen = QGuiApplication::screenAt( currentPos );
      if ( !screen ) {
        return;
      }

      QRect desktop = screen->geometry();

      QSize windowSize = geometry().size();

      int x, y;

      /// Try the to-the-right placement
      if ( currentPos.x() + 4 + windowSize.width() <= desktop.topRight().x() ) {
        x = currentPos.x() + 4;
      }
      else
        /// Try the to-the-left placement
        if ( currentPos.x() - 4 - windowSize.width() >= desktop.x() ) {
          x = currentPos.x() - 4 - windowSize.width();
        }
        else {
          // Center it
          x = desktop.x() + ( desktop.width() - windowSize.width() ) / 2;
        }

      /// Try the to-the-bottom placement
      if ( currentPos.y() + 15 + windowSize.height() <= desktop.bottomLeft().y() ) {
        y = currentPos.y() + 15;
      }
      else
        /// Try the to-the-top placement
        if ( currentPos.y() - 15 - windowSize.height() >= desktop.y() ) {
          y = currentPos.y() - 15 - windowSize.height();
        }
        else {
          // Center it
          y = desktop.y() + ( desktop.height() - windowSize.height() ) / 2;
        }

      move( x, y );
    }
    else {
      if ( pinnedGeometry.size() > 0 ) {
        restoreGeometry( pinnedGeometry );
      }
    }

    show();

    if ( giveFocus ) {
      activateWindow();
      raise();
    }

    if ( !ui.pinButton->isChecked() ) {
      mouseEnteredOnce = false;
      // Need to monitor the mouse so we know when to hide the window
      interceptMouse();
    }

    // This produced some funky mouse grip-related bugs so we commented it out
    // QApplication::processEvents(); // Make window appear immediately no matter what
  }
  else {
    // Pinned-down window isn't always on top, so we need to raise it
    show();
    if ( cfg.preferences.raiseWindowOnSearch ) {
      activateWindow();
      raise();
    }
  }

  if ( ui.pinButton->isChecked() ) {
    setWindowTitle( QString( "%1 - GoldenDict-ng" ).arg( elideInputWord() ) );
  }

  /// Too large strings make window expand which is probably not what user
  /// wants
  translateBox->setText( Folding::escapeWildcardSymbols( pendingWord ), false );

  showTranslationFor( pendingWord );
}

QString ScanPopup::elideInputWord() const
{
  return pendingWord.size() > 32 ? pendingWord.mid( 0, 32 ) + "..." : pendingWord;
}

void ScanPopup::currentGroupChanged( int )
{
  cfg.lastPopupGroupId          = groupList->getCurrentGroup();
  const Instances::Group * igrp = groups.findGroup( cfg.lastPopupGroupId );
  if ( cfg.lastPopupGroupId == GroupId::AllGroupId ) {
    if ( igrp ) {
      igrp->checkMutedDictionaries( &cfg.popupMutedDictionaries );
    }
    dictionaryBar.setMutedDictionaries( &cfg.popupMutedDictionaries );
  }
  else {
    Config::Group * grp = cfg.getGroup( cfg.lastPopupGroupId );
    if ( grp ) {
      if ( igrp ) {
        igrp->checkMutedDictionaries( &grp->popupMutedDictionaries );
      }
      dictionaryBar.setMutedDictionaries( &grp->popupMutedDictionaries );
    }
    else {
      dictionaryBar.setMutedDictionaries( nullptr );
    }
  }

  dictionaryBar.updateToGroup( groups.findGroup( groupList->getCurrentGroup() ),
                               &cfg.popupMutedDictionaries,
                               cfg,
                               true );

  definition->setCurrentGroupId( cfg.lastPopupGroupId );

  if ( isVisible() ) {
    updateSuggestionList();
    QString word = Folding::unescapeWildcardSymbols( definition->getWord() );
    showTranslationFor( word );
  }

  cfg.lastPopupGroupId = groupList->getCurrentGroup();
}

void ScanPopup::translateInputChanged( const QString & text )
{
  updateSuggestionList( text );
  GlobalBroadcaster::instance()->translateLineText = text;
}

void ScanPopup::updateSuggestionList()
{
  updateSuggestionList( translateBox->translateLine()->text() );
}

void ScanPopup::updateSuggestionList( const QString & text )
{
  mainStatusBar->clearMessage();

  QString req = text.trimmed();

  if ( !req.size() ) {
    // An empty request always results in an empty result
    wordFinder.cancel();
    translateBox->setNoResults( false );
    return;
  }

  wordFinder.prefixMatch( req, getActiveDicts() );
}

void ScanPopup::translateInputFinished()
{
  const QString normalizedWord =
    normalizeSmartLookupInput( Folding::unescapeWildcardSymbols( translateBox->translateLine()->text() ) );
  const QStringList smartTerms = detectSmartLookupTerms( normalizedWord );

  pendingWord = chooseSmartLookupQuery( normalizedWord, smartTerms );

  if ( pendingWord.isEmpty() ) {
    return;
  }

  showTranslationFor( pendingWord );
  updateBuddhistGlossaryTab( tabWidget, pendingWord, smartTerms );
  updateWebReferenceTab( tabWidget, pendingWord, smartTerms );

  if ( !smartTerms.isEmpty() && pendingWord != normalizedWord ) {
    showStatusBarMessage( tr( "Smart terms: %1" ).arg( smartTerms.join( QStringLiteral( " | " ) ) ), 8000 );
  }
}

void ScanPopup::showTranslationFor( const QString & word ) const
{
  ui.pronounceButton->setDisabled( true );

  unsigned groupId = groupList->getCurrentGroup();
  definition->showDefinition( word, groupId );
  definition->focus();
  // definition is the first tab
  tabWidget->setTabText( 0, elideInputWord() );
}

const vector< sptr< Dictionary::Class > > & ScanPopup::getActiveDicts()
{
  if ( groups.empty() ) {
    return allDictionaries;
  }

  int current = groupList->currentIndex();

  if ( current < 0 || current >= (int)groups.size() ) {
    return allDictionaries;
  }

  const QSet< QString > * mutedDictionaries = dictionaryBar.getMutedDictionaries();

  if ( !dictionaryBar.toggleViewAction()->isChecked() || mutedDictionaries == nullptr ) {
    return groups[ current ].dictionaries;
  }

  const vector< sptr< Dictionary::Class > > & activeDicts = groups[ current ].dictionaries;

  // Populate the special dictionariesUnmuted array with only unmuted
  // dictionaries

  dictionariesUnmuted.clear();
  dictionariesUnmuted.reserve( activeDicts.size() );

  for ( const auto & activeDict : activeDicts ) {
    if ( !mutedDictionaries->contains( QString::fromStdString( activeDict->getId() ) ) ) {
      dictionariesUnmuted.push_back( activeDict );
    }
  }

  return dictionariesUnmuted;
}

void ScanPopup::typingEvent( const QString & t )
{
  if ( t == "\n" || t == "\r" ) {
    focusTranslateLine();
  }
  else {
    translateBox->translateLine()->clear();
    translateBox->translateLine()->setFocus();
    translateBox->setText( t, true );
    translateBox->translateLine()->setCursorPosition( t.size() );
  }

  updateSuggestionList();
}

bool ScanPopup::eventFilter( QObject * watched, QEvent * event )
{
  if ( watched == translateBox->translateLine() && event->type() == QEvent::FocusIn ) {
    const QFocusEvent * focusEvent = static_cast< QFocusEvent * >( event );

    // select all on mouse click
    if ( focusEvent->reason() == Qt::MouseFocusReason ) {
      QTimer::singleShot( 0, this, &ScanPopup::focusTranslateLine );
    }
    return false;
  }

  if ( mouseIntercepted ) {
    // We're only interested in our events

    if ( event->type() == QEvent::MouseMove ) {
      QMouseEvent * mouseEvent = (QMouseEvent *)event;
      reactOnMouseMove( mouseEvent->globalPosition() );
    }
  }

  if ( event->type() == QEvent::KeyPress && watched != translateBox->translateLine() ) {

    if ( const auto key_event = dynamic_cast< QKeyEvent * >( event );
         key_event->modifiers() == Qt::NoModifier || key_event->modifiers() == Qt::ShiftModifier ) {
      const QString text = key_event->text();

      if ( Utils::ignoreKeyEvent( key_event ) || key_event->key() == Qt::Key_Return
           || key_event->key() == Qt::Key_Enter ) {
        return false; // Those key have other uses than to start typing
      }
      // or don't make sense
      if ( !text.isEmpty() ) {
        typingEvent( text );
        return true;
      }
    }
  }

  return QMainWindow::eventFilter( watched, event );
}

void ScanPopup::reactOnMouseMove( const QPointF & p )
{
  if ( geometry().contains( p.toPoint() ) ) {
    //        qDebug( "got inside" );

    hideTimer.stop();
    mouseEnteredOnce = true;
    uninterceptMouse();
  }
  else {
    //        qDebug( "outside" );
    // We're in grab mode and outside the window - calculate the
    // distance from it. We might want to hide it.

    // When the mouse has entered once, we don't allow it stayng outside,
    // but we give a grace period for it to return.
    int proximity = mouseEnteredOnce ? 0 : 60;

    // Note: watched == this ensures no other child objects popping out are
    // receiving this event, meaning there's basically nothing under the
    // cursor.
    if ( /*watched == this &&*/
         !frameGeometry().adjusted( -proximity, -proximity, proximity, proximity ).contains( p.toPoint() ) ) {
      // We've way too far from the window -- hide the popup

      // If the mouse never entered the popup, hide the window instantly --
      // the user just moved the cursor further away from the window.

      if ( !mouseEnteredOnce ) {
        hideWindow();
      }
      else {
        hideTimer.start();
      }
    }
  }
}

void ScanPopup::mousePressEvent( QMouseEvent * ev )
{
  // With mouse grabs, the press can occur anywhere on the screen, which
  // might mean hiding the window.

  if ( !frameGeometry().contains( ev->globalPosition().toPoint() ) ) {
    hideWindow();

    return;
  }

  if ( ev->button() == Qt::LeftButton ) {
    startPos = ev->globalPosition();
    setCursor( Qt::ClosedHandCursor );
  }

  QMainWindow::mousePressEvent( ev );
}

void ScanPopup::mouseMoveEvent( QMouseEvent * event )
{
  if ( event->buttons() && cursor().shape() == Qt::ClosedHandCursor ) {
    QPointF newPos = event->globalPosition();
    QPointF delta  = newPos - startPos;

    startPos = newPos;

    // Move the window
    move( ( pos() + delta ).toPoint() );
  }

  QMainWindow::mouseMoveEvent( event );
}

void ScanPopup::mouseReleaseEvent( QMouseEvent * ev )
{
  unsetCursor();
  QMainWindow::mouseReleaseEvent( ev );
}

void ScanPopup::leaveEvent( QEvent * event )
{
  QMainWindow::leaveEvent( event );

  // We hide the popup when the mouse leaves it.

  // Combo-boxes seem to generate leave events for their parents when
  // unfolded, so we check coordinates as well.
  // If the dialog is pinned, we don't hide the popup.
  // If some mouse buttons are pressed, we don't hide the popup either,
  // since it indicates the move operation is underway.
  if ( !ui.pinButton->isChecked() && !geometry().contains( QCursor::pos() )
       && QApplication::mouseButtons() == Qt::NoButton ) {
    hideTimer.start();
  }
}

void ScanPopup::enterEvent( QEnterEvent * event )
{
  QMainWindow::enterEvent( event );

  if ( mouseEnteredOnce ) {
    // We "enter" first time via our event filter. This seems to evade some
    // unexpected behavior under Windows.

    // If there was a countdown to hide the window, stop it.
    hideTimer.stop();
  }
}

void ScanPopup::showEvent( QShowEvent * ev )
{
  QMainWindow::showEvent( ev );

  if ( groups.size() <= 1 ) { // Only the default group? Hide then.
    groupListAction->setVisible( false );
  }

  if ( dictionaryBar.isVisible() ) {
    dictionaryBar.updateToGroup( groups.findGroup( groupList->getCurrentGroup() ),
                                 &cfg.popupMutedDictionaries,
                                 cfg,
                                 true );
    setDictionaryIconSize();
  }
}

void ScanPopup::closeEvent( QCloseEvent * ev )
{
  if ( isVisible() && ui.pinButton->isChecked() ) {
    pinnedGeometry = saveGeometry();
  }

  QMainWindow::closeEvent( ev );
}

void ScanPopup::moveEvent( QMoveEvent * ev )
{
  if ( isVisible() && ui.pinButton->isChecked() ) {
    pinnedGeometry = saveGeometry();
  }

  QMainWindow::moveEvent( ev );
}

void ScanPopup::prefixMatchFinished()
{
  // Check that there's a window there at all
  if ( isVisible() ) {
    if ( wordFinder.getErrorString().size() ) {
      showStatusBarMessage( tr( "WARNING: %1" ).arg( wordFinder.getErrorString() ),
                            20000,
                            QPixmap( ":/icons/error.svg" ) );
    }
    else {
      auto results = wordFinder.getResults();
      QStringList _results;
      for ( const auto & [ fst, snd ] : results ) {
        _results << fst;
      }

      // Reset the noResults mark if it's on right now
      translateBox->setNoResults( _results.isEmpty() );
      translateBox->setModel( _results );
    }
  }
}

void ScanPopup::pronounceButton_clicked() const
{
  definition->playSound();
}

void ScanPopup::saveArticleButton_clicked()
{
  // Delegate to centralized saver object; ScanPopup will display status messages
  auto * saver = new ArticleSaver( this, definition, cfg );
  connect( saver, &ArticleSaver::statusMessage, this, [ this ]( const QString & message, int timeout ) {
    showStatusBarMessage( message, timeout );
  } );
  saver->save();
}

void ScanPopup::pinButtonClicked( bool checked )
{
  if ( checked ) {
    uninterceptMouse();

    ui.onTopButton->setVisible( true );
    Qt::WindowFlags flags = pinnedWindowFlags;
    if ( ui.onTopButton->isChecked() ) {
      flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags( flags );

#ifdef Q_OS_MACOS
    setAttribute( Qt::WA_MacAlwaysShowToolWindow );
#endif

    setWindowTitle( QString( "%1 - GoldenDict-ng" ).arg( elideInputWord() ) );
    hideTimer.stop();
  }
  else {
    ui.onTopButton->setVisible( false );
    setWindowFlags( unpinnedWindowFlags() );

#ifdef Q_OS_MACOS
    setAttribute( Qt::WA_MacAlwaysShowToolWindow, false );
#endif

    mouseEnteredOnce = true;
  }
  cfg.pinPopupWindow = checked;

  show();

  if ( checked ) {
    pinnedGeometry = saveGeometry();
  }
}

void ScanPopup::focusTranslateLine()
{
  if ( !isActiveWindow() ) {
    activateWindow();
  }

  translateBox->translateLine()->setFocus();
  translateBox->translateLine()->selectAll();
}

void ScanPopup::stopAudio() const
{
  if ( auto view = qobject_cast< ArticleView * >( tabWidget->currentWidget() ) ) {
    view->stopSound();
  }
}

void ScanPopup::dictionaryBar_visibility_changed( bool visible )
{
  if ( visible ) {
    dictionaryBar.updateToGroup( groups.findGroup( groupList->getCurrentGroup() ),
                                 &cfg.popupMutedDictionaries,
                                 cfg,
                                 true );
    setDictionaryIconSize();
    definition->updateMutedContents();
  }
}

void ScanPopup::hideTimerExpired()
{
  if ( isVisible() ) {
    hideWindow();
  }
}

void ScanPopup::pageLoaded( ArticleView * ) const
{
  if ( !isVisible() ) {
    return;
  }
  auto pronounceBtn = ui.pronounceButton;
  definition->hasSound( [ pronounceBtn ]( bool has ) {
    if ( pronounceBtn ) {
      pronounceBtn->setDisabled( !has );
    }
  } );

  updateBackForwardButtons();
}

void ScanPopup::showStatusBarMessage( const QString & message, int timeout, const QPixmap & icon )
{
  mainStatusBar->showMessage( message, timeout, icon );
}

void ScanPopup::escapePressed()
{
  if ( !definition->closeSearch() ) {
    hideWindow();
  }
}

void ScanPopup::hideWindow()
{
  uninterceptMouse();

  emit closeMenu();
  hideTimer.stop();
  unsetCursor();
  translateBox->setPopupEnabled( false );
  translateBox->translateLine()->deselect();
  hide();
  definition->clearContent();
}

void ScanPopup::interceptMouse()
{
  if ( !mouseIntercepted ) {
    // We used to grab the mouse -- but this doesn't always work reliably
    // (e.g. doesn't work at all in Windows 7 for some reason). Therefore
    // we use a polling timer now.

    //    grabMouse();
    mouseGrabPollTimer.start();

    qApp->installEventFilter( this );

    mouseIntercepted = true;
  }
}

void ScanPopup::mouseGrabPoll()
{
  if ( mouseIntercepted ) {
    reactOnMouseMove( QCursor::pos() );
  }
}

void ScanPopup::uninterceptMouse()
{
  if ( mouseIntercepted ) {
    qApp->removeEventFilter( this );
    mouseGrabPollTimer.stop();
    //    releaseMouse();

    mouseIntercepted = false;
  }
}

void ScanPopup::mutedDictionariesChanged()
{
  updateSuggestionList();
  if ( dictionaryBar.toggleViewAction()->isChecked() ) {
    definition->updateMutedContents();
  }
}

void ScanPopup::sendWordButton_clicked()
{
  if ( !isVisible() ) {
    return;
  }
  if ( !ui.pinButton->isChecked() ) {
    definition->closeSearch();
    hideWindow();
  }
  emit sendPhraseToMainWindow( definition->getWord() );
}

void ScanPopup::sendWordToFavoritesButton_clicked()
{
  if ( !isVisible() ) {
    return;
  }
  auto current_exist = isWordPresentedInFavorites( definition->getTitle() );
  // if current_exist=false( not exist ),  after click ,the word should be in the favorite which is blueStar
  ui.sendWordToFavoritesButton->setIcon( !current_exist ? blueStarIcon : starIcon );
  emit sendWordToFavorites( definition->getTitle() );
}

void ScanPopup::switchExpandOptionalPartsMode()
{
  if ( isVisible() ) {
    emit switchExpandMode();
  }
}

void ScanPopup::updateBackForwardButtons() const
{
  if ( auto view = qobject_cast< ArticleView * >( tabWidget->currentWidget() ) ) {
    ui.goBackButton->setEnabled( view->canGoBack() );
    ui.goForwardButton->setEnabled( view->canGoForward() );
  }
}

void ScanPopup::goBackButton_clicked() const
{
  if ( auto view = qobject_cast< ArticleView * >( tabWidget->currentWidget() ) ) {
    view->back();
  }
}

void ScanPopup::goForwardButton_clicked() const
{
  if ( auto view = qobject_cast< ArticleView * >( tabWidget->currentWidget() ) ) {
    view->forward();
  }
}

void ScanPopup::setDictionaryIconSize()
{
  if ( cfg.usingToolbarsIconSize == Config::ToolbarsIconSize::Small ) {
    dictionaryBar.setDictionaryIconSize( DictionaryBar::IconSize::Small );
  }
  else if ( cfg.usingToolbarsIconSize == Config::ToolbarsIconSize::Normal ) {
    dictionaryBar.setDictionaryIconSize( DictionaryBar::IconSize::Normal );
  }
  else if ( cfg.usingToolbarsIconSize == Config::ToolbarsIconSize::Large ) {
    dictionaryBar.setDictionaryIconSize( DictionaryBar::IconSize::Large );
  }

  QSize iconSize = dictionaryBar.iconSize();

  ui.goBackButton->setIconSize( iconSize );
  ui.goForwardButton->setIconSize( iconSize );
  ui.pronounceButton->setIconSize( iconSize );
  ui.sendWordButton->setIconSize( iconSize );
  ui.saveArticleButton->setIconSize( iconSize );
  ui.sendWordToFavoritesButton->setIconSize( iconSize );
  ui.onTopButton->setIconSize( iconSize );
  ui.pinButton->setIconSize( iconSize );

  int buttonSize = iconSize.width() + 8;
  ui.goBackButton->setFixedSize( buttonSize, buttonSize );
  ui.goForwardButton->setFixedSize( buttonSize, buttonSize );
  ui.pronounceButton->setFixedSize( buttonSize, buttonSize );
  ui.sendWordButton->setFixedSize( buttonSize, buttonSize );
  ui.saveArticleButton->setFixedSize( buttonSize, buttonSize );
  ui.sendWordToFavoritesButton->setFixedSize( buttonSize, buttonSize );
  ui.onTopButton->setFixedSize( buttonSize, buttonSize );
  ui.pinButton->setFixedSize( buttonSize, buttonSize );
}


void ScanPopup::setGroupByName( const QString & name ) const
{
  int i;
  for ( i = 0; i < groupList->count(); i++ ) {
    if ( groupList->itemText( i ) == name ) {
      groupList->setCurrentIndex( i );
      break;
    }
  }
  if ( i >= groupList->count() ) {
    qWarning( "Group \"%s\" for popup window is not found", name.toUtf8().data() );
  }
}

void ScanPopup::openSearch()
{
  definition->openSearch();
}

void ScanPopup::alwaysOnTopClicked( bool checked )
{
  bool wasVisible = isVisible();
  if ( ui.pinButton->isChecked() ) {
    Qt::WindowFlags flags = this->windowFlags();
    if ( checked ) {
      setWindowFlags( flags | Qt::WindowStaysOnTopHint );
    }
    else {
      setWindowFlags( flags ^ Qt::WindowStaysOnTopHint );
    }
    if ( wasVisible ) {
      show();
    }
  }
}

void ScanPopup::titleChanged( ArticleView * view, const QString & title ) const
{
  // Set icon for "Add to Favorites" button
  ui.sendWordToFavoritesButton->setIcon( isWordPresentedInFavorites( title ) ? blueStarIcon : starIcon );

  int index = tabWidget->indexOf( view );
  if ( index != -1 ) {
    // Truncate long titles to make tab labels more readable
    const int maxTabTitleLength = 30;
    QString tabTitle            = Utils::ellipsizeString( title, maxTabTitleLength );
    tabWidget->setTabText( index, tabTitle );
    tabWidget->setTabToolTip( index, title );
  }
}

void ScanPopup::activeArticleChanged( const ArticleView * view, const QString & id )
{
  if ( view != tabWidget->currentWidget() || view->isWebsite() ) {
    return;
  }
}

ArticleView * ScanPopup::findArticleViewByDictId( const QString & dictId )
{
  if ( cfg.preferences.openWebsiteInNewTab ) {
    for ( int i = 0; i < tabWidget->count(); i++ ) {
      auto * view = qobject_cast< ArticleView * >( tabWidget->widget( i ) );
      if ( view && view->isWebsite() && view->getActiveArticleId() == dictId ) {
        return view;
      }
    }
  }
  return nullptr;
}

void ScanPopup::openWebsiteInNewTab( QString name, QString url, QString dictId, bool isPopup, QString word )
{
  if ( !isVisible() ) {
    return;
  }

  if ( !isPopup ) {
    return;
  }

  // Look for an existing tab with the same dictionary id
  for ( int i = 0; i < tabWidget->count(); ++i ) {
    if ( auto view = qobject_cast< ArticleView * >( tabWidget->widget( i ) ) ) {
      if ( view->isWebsite() && view->getActiveArticleId() == dictId ) {
        // Set the current word for the existing website view
        if ( !word.isEmpty() ) {
          view->setCurrentWord( word );
        }
        view->load( url, name );
        // Truncate long website names for tab labels
        const int maxTabTitleLength = 30;
        QString truncatedName       = Utils::ellipsizeString( name, maxTabTitleLength );
        tabWidget->setTabText( i, truncatedName );
        return;
      }
    }
  }

  auto view = new ArticleView( tabWidget,
                               articleNetMgr,
                               true,
                               cfg,
                               translateBox->translateLine(),
                               dictionaryBar.toggleViewAction(),
                               groupList->getCurrentGroup() );

  view->setWebsite( true );
  view->setWebsiteHost( QUrl( url ).host() );
  view->setActiveArticleId( dictId );
  // Set the current word for the new website view
  if ( !word.isEmpty() ) {
    view->setCurrentWord( word );
  }

  // Connect vital signals
  connect( view, &ArticleView::inspectSignal, this, &ScanPopup::inspectElementWhenPinned );
  connect( view, &ArticleView::forceAddWordToHistory, this, &ScanPopup::forceAddWordToHistory );
  connect( this, &ScanPopup::closeMenu, view, &ArticleView::closePopupMenu );
  connect( view, &ArticleView::sendWordToHistory, this, &ScanPopup::sendWordToHistory );
  connect( view, &ArticleView::typingEvent, this, &ScanPopup::typingEvent );
  connect( view, &ArticleView::statusBarMessage, this, &ScanPopup::showStatusBarMessage );
  connect( view, &ArticleView::pageLoaded, this, &ScanPopup::pageLoaded );
  connect( view, &ArticleView::titleChanged, this, &ScanPopup::titleChanged );
  connect( view, &ArticleView::sendWordToInputLine, this, &ScanPopup::translateWord );

  // Truncate long website names for tab labels
  const int maxTabTitleLength = 30;
  QString truncatedName       = Utils::ellipsizeString( name, maxTabTitleLength );

  int index = tabWidget->addTab( view, truncatedName );
  tabWidget->setCurrentIndex( index );

  view->load( url, name );
}

bool ScanPopup::isWordPresentedInFavorites( const QString & word ) const
{
  return GlobalBroadcaster::instance()->isWordPresentedInFavorites( word );
}

#ifdef WITH_X11
void ScanPopup::showScanFlag()
{
  if ( scanFlag ) {
    scanFlag->showScanFlag();
  }
}

void ScanPopup::hideScanFlag()
{
  if ( scanFlag ) {
    scanFlag->hideWindow();
  }
}
#endif
