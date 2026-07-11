/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "config.hh"
#include "logger.hh"
#include "mainwindow.hh"
#include "version.hh"
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPoint>
#include <QProcess>
#include <QSettings>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>
#endif
#include <QByteArray>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QSessionManager>
#include <QString>
#include <QtWebEngineCore/QWebEngineUrlScheme>
#include <stdio.h>
#if defined( Q_OS_UNIX )
  #include "unix/ksignalhandler.hh"
#endif

#ifdef Q_OS_MACOS
  #include "macos/mac_url_handler.hh"
  #include <QDesktopServices>
#endif

#ifdef Q_OS_WIN32
  #include <QStyleFactory>
#endif


#ifdef Q_OS_WIN
#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>
  #include "hotkey/winhotkeyapplication.hh"
using GD_QApplication = QHotkeyApplication;
#else
  #include "qtsingleapplication.h"
using GD_QApplication = QtSingleApplication;
#endif

#if defined( USE_BREAKPAD )
  #if defined( Q_OS_MAC )
    #include "client/mac/handler/exception_handler.h"
  #elif defined( Q_OS_WIN32 )
    #include "client/windows/handler/exception_handler.h"
  #endif
#endif

#if defined( USE_BREAKPAD )
  #ifdef Q_OS_WIN32
bool callback( const wchar_t * dump_path,
               const wchar_t * id,
               void * context,
               EXCEPTION_POINTERS * exinfo,
               MDRawAssertionInfo * assertion,
               bool succeeded )
{
  if ( succeeded ) {
    qDebug() << "Create dump file success";
  }
  else {
    qDebug() << "Create dump file failed";
  }
  return succeeded;
}
  #endif
  #ifdef Q_OS_MAC
bool callback( const char * dump_dir, const char * minidump_id, void * context, bool succeeded )
{
  if ( succeeded ) {
    qDebug() << "Create dump file success";
  }
  else {
    qDebug() << "Create dump file failed";
  }
  return succeeded;
}
  #endif
#endif

struct GDOptions
{
  bool logFile     = false;
  bool togglePopup = false;
  QString word, groupName, popupGroupName;
  QString window;

  inline bool needSetGroup() const
  {
    return !groupName.isEmpty();
  }

  inline QString getGroupName() const
  {
    return groupName;
  }

  inline bool needSetPopupGroup() const
  {
    return !popupGroupName.isEmpty();
  }

  inline QString getPopupGroupName() const
  {
    return popupGroupName;
  }

  inline bool needTranslateWord() const
  {
    return !word.isEmpty();
  }

  inline QString wordToTranslate() const
  {
    return word;
  }

  inline bool needTogglePopup() const
  {
    return togglePopup;
  }
  bool notts;
  bool resetState = false;
};

void processCommandLine( QCoreApplication * app, GDOptions * result )
{
  QCommandLineParser qcmd;

  qcmd.setApplicationDescription( QObject::tr( "A dictionary lookup program." ) );
  qcmd.addHelpOption(); // -h --help

  qcmd.addPositionalArgument( "word", QObject::tr( "Word or sentence to query." ), "[word]" );

  QCommandLineOption logFileOption( QStringList() << "l"
                                                  << "log-to-file",
                                    QObject::tr( "Save debug messages to gd_log.txt in the config folder." ) );

  QCommandLineOption resetState( QStringList() << "r"
                                               << "reset-window-state",
                                 QObject::tr( "Reset window state." ) );

  QCommandLineOption notts( "no-tts", QObject::tr( "Disable tts." ) );

  QCommandLineOption groupNameOption( QStringList() << "g"
                                                    << "group-name",
                                      QObject::tr( "Change the group of main window." ),
                                      "groupName" );
  QCommandLineOption popupGroupNameOption( QStringList() << "p"
                                                         << "popup-group-name",
                                           QObject::tr( "Change the group of popup." ),
                                           "popupGroupName" );

  QCommandLineOption window_popupOption( QStringList() << "s"
                                                       << "scanpopup"
                                                       << "popup",
                                         QObject::tr( "Force the word to be translated in Popup." ) );

  QCommandLineOption window_mainWindowOption( QStringList() << "m"
                                                            << "main-window",
                                              QObject::tr( "Force the word to be translated in the mainwindow." ) );

  QCommandLineOption togglePopupOption( QStringList() << "t"
                                                      << "toggle-popup",
                                        QObject::tr( "Toggle popup." ) );

  QCommandLineOption printVersion( QStringList() << "v"
                                                 << "version",
                                   QObject::tr( "Print version and diagnosis info." ) );

  qcmd.addOption( logFileOption );
  qcmd.addOption( groupNameOption );
  qcmd.addOption( popupGroupNameOption );
  qcmd.addOption( window_popupOption );
  qcmd.addOption( window_mainWindowOption );
  qcmd.addOption( togglePopupOption );
  qcmd.addOption( notts );
  qcmd.addOption( resetState );
  qcmd.addOption( printVersion );
  qcmd.process( *app );

  if ( qcmd.isSet( logFileOption ) ) {
    result->logFile = true;
  }

  if ( qcmd.isSet( groupNameOption ) ) {
    result->groupName = qcmd.value( groupNameOption );
  }

  if ( qcmd.isSet( popupGroupNameOption ) ) {
    result->popupGroupName = qcmd.value( popupGroupNameOption );
  }
  if ( qcmd.isSet( window_popupOption ) ) {
    result->window = "popup";
  }
  if ( qcmd.isSet( window_mainWindowOption ) ) {
    result->window = "main";
  }
  if ( qcmd.isSet( togglePopupOption ) ) {
    result->togglePopup = true;
  }

  if ( qcmd.isSet( notts ) ) {
    result->notts = true;
  }

  if ( qcmd.isSet( resetState ) ) {
    result->resetState = true;
  }

  if ( qcmd.isSet( printVersion ) ) {
    qInfo() << qPrintable( Version::everything() );
    std::exit( 0 );
  }

  const QStringList posArgs = qcmd.positionalArguments();
  if ( !posArgs.empty() ) {
    QString originalArg = posArgs.at( 0 );

#if defined( Q_OS_LINUX ) || defined( Q_OS_WIN )
    // handle url scheme like "goldendict://" or "dict://" on windows/linux
    auto schemePos = originalArg.indexOf( "://" );
    if ( schemePos != -1 ) {
      QUrl url( originalArg );
      QString query = url.query();

      QString word = Utils::Url::extractWordFromUrl( originalArg );

      result->word = word;

      if ( !query.isEmpty() ) {
        QUrlQuery urlQuery( query );
        QString targetParam = urlQuery.queryItemValue( "target" );

        if ( targetParam == "popup" ) {
          result->window = "popup";
        }
        else if ( targetParam == "main" ) {
          result->window = "main";
        }
      }
    }
    else {
      result->word = originalArg;
    }
#else
    result->word = originalArg;
#endif
  }
}


#ifdef Q_OS_WIN

#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>
namespace {

enum class SutraStartupMouseLookupMode
{
  Disabled       = 0,
  CtrlRightClick = 1,
  CtrlLeftClick  = 2,
  AltRightClick  = 3
};

QString sutraStartupMouseLookupModeSettingsKey()
{
  return QStringLiteral( "SutraEdition/MouseLookupMode" );
}

SutraStartupMouseLookupMode loadSutraStartupMouseLookupMode()
{
  QSettings settings;
  const int value = qBound( 0,
                            settings.value( sutraStartupMouseLookupModeSettingsKey(), 1 ).toInt(),
                            3 );
  return static_cast< SutraStartupMouseLookupMode >( value );
}

void sendSutraStartupVirtualKey( WORD virtualKey, bool down )
{
  INPUT input = {};
  input.type   = INPUT_KEYBOARD;
  input.ki.wVk = virtualKey;

  if ( !down ) {
    input.ki.dwFlags = KEYEVENTF_KEYUP;
  }

  SendInput( 1, &input, sizeof( INPUT ) );
}

void releaseSutraStartupControlKeys()
{
  sendSutraStartupVirtualKey( VK_CONTROL, false );
  sendSutraStartupVirtualKey( VK_LCONTROL, false );
  sendSutraStartupVirtualKey( VK_RCONTROL, false );
}

void releaseSutraStartupAltKeys()
{
  sendSutraStartupVirtualKey( VK_MENU, false );
  sendSutraStartupVirtualKey( VK_LMENU, false );
  sendSutraStartupVirtualKey( VK_RMENU, false );
}

void sendSutraStartupCtrlC()
{
  INPUT inputs[ 4 ] = {};

  inputs[ 0 ].type   = INPUT_KEYBOARD;
  inputs[ 0 ].ki.wVk = VK_CONTROL;

  inputs[ 1 ].type   = INPUT_KEYBOARD;
  inputs[ 1 ].ki.wVk = 'C';

  inputs[ 2 ].type       = INPUT_KEYBOARD;
  inputs[ 2 ].ki.wVk     = 'C';
  inputs[ 2 ].ki.dwFlags = KEYEVENTF_KEYUP;

  inputs[ 3 ].type       = INPUT_KEYBOARD;
  inputs[ 3 ].ki.wVk     = VK_CONTROL;
  inputs[ 3 ].ki.dwFlags = KEYEVENTF_KEYUP;

  SendInput( 4, inputs, sizeof( INPUT ) );
}

void sendSutraStartupLeftDoubleClickAt( const QPoint & globalPos )
{
  SetCursorPos( globalPos.x(), globalPos.y() );

  INPUT inputs[ 4 ] = {};

  inputs[ 0 ].type       = INPUT_MOUSE;
  inputs[ 0 ].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

  inputs[ 1 ].type       = INPUT_MOUSE;
  inputs[ 1 ].mi.dwFlags = MOUSEEVENTF_LEFTUP;

  inputs[ 2 ].type       = INPUT_MOUSE;
  inputs[ 2 ].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

  inputs[ 3 ].type       = INPUT_MOUSE;
  inputs[ 3 ].mi.dwFlags = MOUSEEVENTF_LEFTUP;

  SendInput( 4, inputs, sizeof( INPUT ) );
}


void sendSutraStartupLeftClickAt( const QPoint & globalPos )
{
  SetCursorPos( globalPos.x(), globalPos.y() );

  INPUT inputs[ 2 ] = {};

  inputs[ 0 ].type       = INPUT_MOUSE;
  inputs[ 0 ].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

  inputs[ 1 ].type       = INPUT_MOUSE;
  inputs[ 1 ].mi.dwFlags = MOUSEEVENTF_LEFTUP;

  SendInput( 2, inputs, sizeof( INPUT ) );
}

void sendSutraStartupLineSelectionAt( const QPoint & globalPos )
{
  sendSutraStartupLeftClickAt( globalPos );

  INPUT inputs[ 6 ] = {};

  // Home
  inputs[ 0 ].type   = INPUT_KEYBOARD;
  inputs[ 0 ].ki.wVk = VK_HOME;

  inputs[ 1 ].type       = INPUT_KEYBOARD;
  inputs[ 1 ].ki.wVk     = VK_HOME;
  inputs[ 1 ].ki.dwFlags = KEYEVENTF_KEYUP;

  // Shift down
  inputs[ 2 ].type   = INPUT_KEYBOARD;
  inputs[ 2 ].ki.wVk = VK_SHIFT;

  // End
  inputs[ 3 ].type   = INPUT_KEYBOARD;
  inputs[ 3 ].ki.wVk = VK_END;

  inputs[ 4 ].type       = INPUT_KEYBOARD;
  inputs[ 4 ].ki.wVk     = VK_END;
  inputs[ 4 ].ki.dwFlags = KEYEVENTF_KEYUP;

  // Shift up
  inputs[ 5 ].type       = INPUT_KEYBOARD;
  inputs[ 5 ].ki.wVk     = VK_SHIFT;
  inputs[ 5 ].ki.dwFlags = KEYEVENTF_KEYUP;

  SendInput( 6, inputs, sizeof( INPUT ) );
}
static bool sutraStartupIsCjkChar( const QChar & ch )
{
  const uint u = ch.unicode();

  return ( u >= 0x3400 && u <= 0x4DBF )   // CJK Extension A
      || ( u >= 0x4E00 && u <= 0x9FFF )   // CJK Unified Ideographs
      || ( u >= 0xF900 && u <= 0xFAFF )   // CJK Compatibility Ideographs
      || ( u >= 0x3000 && u <= 0x303F )   // CJK Symbols and Punctuation
      || ( u >= 0x2E80 && u <= 0x2EFF )   // CJK Radicals Supplement
      || ( u >= 0x2F00 && u <= 0x2FDF );  // Kangxi Radicals
}

bool sutraStartupIsTooShortCjkLookupText( const QString & text )
{
  const QString trimmed = text.trimmed();

  if ( trimmed.size() > 2 ) {
    return false;
  }

  for ( const QChar & ch : trimmed ) {
    if ( sutraStartupIsCjkChar( ch ) ) {
      return true;
    }
  }

  return false;
}
bool sutraStartupHasLatinLetter( const QString & text )
{
  for ( const QChar & ch : text ) {
    if ( ch.isLetter() && ch.unicode() < 0x024F ) {
      return true;
    }
  }

  return false;
}

QString sutraStartupNormalizeVietnameseLookupText( const QString & text )
{
  const QString decomposed = text.toCaseFolded().normalized( QString::NormalizationForm_D );

  QString normalized;
  normalized.reserve( decomposed.size() );

  bool lastWasSpace = true;

  for ( const QChar & ch : decomposed ) {
    const QChar::Category category = ch.category();

    if ( category == QChar::Mark_NonSpacing
      || category == QChar::Mark_SpacingCombining
      || category == QChar::Mark_Enclosing ) {
      continue;
    }

    QString piece;

    if ( ch == QChar( 0x0111 ) || ch == QChar( 0x0110 ) ) {
      piece = QStringLiteral( "d" );
    }
    else if ( ch.isLetterOrNumber() ) {
      piece = QString( ch );
    }
    else if ( ch.isSpace() || ch == QLatin1Char( '-' ) || ch == QLatin1Char( '_' ) || ch == QChar( 0x2013 ) || ch == QChar( 0x2014 ) ) {
      piece = QStringLiteral( " " );
    }
    else {
      piece = QStringLiteral( " " );
    }

    if ( piece == QStringLiteral( " " ) ) {
      if ( !lastWasSpace ) {
        normalized += QLatin1Char( ' ' );
      }

      lastWasSpace = true;
    }
    else {
      normalized += piece;
      lastWasSpace = false;
    }
  }

  return normalized.trimmed();
}

bool sutraStartupIsSingleLatinWord( const QString & text )
{
  const QString trimmed = text.trimmed();

  if ( trimmed.isEmpty() || trimmed.size() > 32 ) {
    return false;
  }

  if ( trimmed.contains( QRegularExpression( QStringLiteral( "\\s" ) ) ) ) {
    return false;
  }

  return sutraStartupHasLatinLetter( trimmed );
}

bool sutraStartupShouldUseLineFallbackForPhrase( const QString & text )
{
  return sutraStartupIsTooShortCjkLookupText( text )
      || sutraStartupIsSingleLatinWord( text );
}

QStringList sutraStartupVietnameseBuddhistPhrases()
{
  return QStringList{
    QStringLiteral( "A Di ÄÃ  Pháº­t" ),
    QStringLiteral( "BÃ¡t chÃ¡nh Ä‘áº¡o" ),
    QStringLiteral( "BÃ¡t-nhÃ£ Ba-la-máº­t-Ä‘a" ),
    QStringLiteral( "BÃ¡t-nhÃ£ Ba-la-máº­t" ),
    QStringLiteral( "Bá»“ Äá» Äáº¡t Ma" ),
    QStringLiteral( "Bá»“ Ä‘á» tÃ¢m" ),
    QStringLiteral( "Bá»“ TÃ¡t" ),
    QStringLiteral( "Bá»‘ thÃ­" ),
    QStringLiteral( "ChÃ¡nh Ä‘á»‹nh" ),
    QStringLiteral( "ChÃ¡nh kiáº¿n" ),
    QStringLiteral( "ChÃ¡nh máº¡ng" ),
    QStringLiteral( "ChÃ¡nh ngá»¯" ),
    QStringLiteral( "ChÃ¡nh nghiá»‡p" ),
    QStringLiteral( "ChÃ¡nh niá»‡m" ),
    QStringLiteral( "ChÃ¡nh tinh táº¥n" ),
    QStringLiteral( "ChÃ¡nh tÆ° duy" ),
    QStringLiteral( "ChÃ¢n nhÆ°" ),
    QStringLiteral( "ChÃºng sinh" ),
    QStringLiteral( "Diá»‡u Ä‘áº¿" ),
    QStringLiteral( "DuyÃªn khá»Ÿi" ),
    QStringLiteral( "GiÃ¡c ngá»™" ),
    QStringLiteral( "Giá»›i Ä‘á»‹nh tuá»‡" ),
    QStringLiteral( "Há»¯u tÃ¬nh" ),
    QStringLiteral( "Khá»• Ä‘áº¿" ),
    QStringLiteral( "KhÃ´ng tá»©c thá»‹ sáº¯c" ),
    QStringLiteral( "Lá»¥c Ä‘á»™" ),
    QStringLiteral( "LuÃ¢n há»“i" ),
    QStringLiteral( "Niáº¿t bÃ n" ),
    QStringLiteral( "NgÅ© uáº©n" ),
    QStringLiteral( "PhÃ¡p thÃ¢n" ),
    QStringLiteral( "Pháº­t phÃ¡p" ),
    QStringLiteral( "Pháº­t tÃ¡nh" ),
    QStringLiteral( "Pháº­t tÃ­nh" ),
    QStringLiteral( "QuÃ¡n Tháº¿ Ã‚m" ),
    QStringLiteral( "QuÃ¡n Tháº¿ Ã‚m Bá»“ TÃ¡t" ),
    QStringLiteral( "QuÃ¡n Tá»± Táº¡i" ),
    QStringLiteral( "QuÃ¡n Tá»± Táº¡i Bá»“ TÃ¡t" ),
    QStringLiteral( "Sáº¯c tá»©c thá»‹ khÃ´ng" ),
    QStringLiteral( "Tam báº£o" ),
    QStringLiteral( "Tam Ä‘á»™c" ),
    QStringLiteral( "Tam há»c" ),
    QStringLiteral( "TÃ¡nh khÃ´ng" ),
    QStringLiteral( "TÃ¢m vÃ´ quÃ¡i ngáº¡i" ),
    QStringLiteral( "Thiá»n Ä‘á»‹nh" ),
    QStringLiteral( "Tá»© diá»‡u Ä‘áº¿" ),
    QStringLiteral( "Tá»© niá»‡m xá»©" ),
    QStringLiteral( "Tá»© thÃ¡nh Ä‘áº¿" ),
    QStringLiteral( "Tá»« bi" ),
    QStringLiteral( "VÃ´ minh" ),
    QStringLiteral( "VÃ´ ngÃ£" ),
    QStringLiteral( "VÃ´ thÆ°á»ng" ),
    QStringLiteral( "VÃ´ thÆ°á»£ng chÃ¡nh Ä‘áº³ng chÃ¡nh giÃ¡c" )
  };
}

QString sutraStartupVietnameseWindowAroundAnchor( const QString & lineText, const QString & anchorText )
{
  const QString anchor = sutraStartupNormalizeVietnameseLookupText( anchorText );

  if ( anchor.isEmpty() ) {
    return {};
  }

  QString simplifiedLine = lineText;
  simplifiedLine.replace( QRegularExpression( QStringLiteral( "[\\r\\n\\t]+" ) ), QStringLiteral( " " ) );
  simplifiedLine.replace( QRegularExpression( QStringLiteral( "[,.;:!?()\\[\\]{}<>\"â€œâ€'â€˜â€™]+" ) ), QStringLiteral( " " ) );

#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
  const QStringList words = simplifiedLine.split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
#else
  const QStringList words = simplifiedLine.split( QRegularExpression( QStringLiteral( "\\s+" ) ), QString::SkipEmptyParts );
#endif

  if ( words.isEmpty() ) {
    return {};
  }

  int anchorIndex = -1;

  for ( int i = 0; i < words.size(); ++i ) {
    if ( sutraStartupNormalizeVietnameseLookupText( words.at( i ) ) == anchor ) {
      anchorIndex = i;
      break;
    }
  }

  if ( anchorIndex < 0 ) {
    return {};
  }

  // Prefer common Vietnamese compounds: previous + current, current + next, then 3-word windows.
  QStringList candidates;

  if ( anchorIndex > 0 ) {
    candidates << QStringList{ words.at( anchorIndex - 1 ), words.at( anchorIndex ) }.join( QLatin1Char( ' ' ) );
  }

  if ( anchorIndex + 1 < words.size() ) {
    candidates << QStringList{ words.at( anchorIndex ), words.at( anchorIndex + 1 ) }.join( QLatin1Char( ' ' ) );
  }

  if ( anchorIndex > 0 && anchorIndex + 1 < words.size() ) {
    candidates << QStringList{ words.at( anchorIndex - 1 ), words.at( anchorIndex ), words.at( anchorIndex + 1 ) }.join( QLatin1Char( ' ' ) );
  }

  if ( anchorIndex + 2 < words.size() ) {
    candidates << QStringList{ words.at( anchorIndex ), words.at( anchorIndex + 1 ), words.at( anchorIndex + 2 ) }.join( QLatin1Char( ' ' ) );
  }

  if ( anchorIndex > 1 ) {
    candidates << QStringList{ words.at( anchorIndex - 2 ), words.at( anchorIndex - 1 ), words.at( anchorIndex ) }.join( QLatin1Char( ' ' ) );
  }

  for ( const QString & candidate : candidates ) {
    if ( candidate.trimmed().size() >= anchorText.trimmed().size() ) {
      return candidate.trimmed();
    }
  }

  return words.at( anchorIndex ).trimmed();
}

QString sutraStartupBestVietnamesePhraseFromLine( const QString & lineText, const QString & anchorText )
{
  const QString normalizedLine   = sutraStartupNormalizeVietnameseLookupText( lineText );
  const QString normalizedAnchor = sutraStartupNormalizeVietnameseLookupText( anchorText );

  if ( normalizedLine.isEmpty() ) {
    return {};
  }

  QString bestPhrase;
  int bestScore = -1;

  for ( const QString & phrase : sutraStartupVietnameseBuddhistPhrases() ) {
    const QString normalizedPhrase = sutraStartupNormalizeVietnameseLookupText( phrase );

    if ( normalizedPhrase.isEmpty() || !normalizedLine.contains( normalizedPhrase ) ) {
      continue;
    }

    if ( !normalizedAnchor.isEmpty() && !normalizedPhrase.contains( normalizedAnchor ) ) {
      continue;
    }

    if ( normalizedPhrase.size() > bestScore ) {
      bestScore = normalizedPhrase.size();
      bestPhrase = phrase;
    }
  }

  if ( !bestPhrase.isEmpty() ) {
    return bestPhrase;
  }

  if ( sutraStartupIsSingleLatinWord( anchorText ) ) {
    const QString windowPhrase = sutraStartupVietnameseWindowAroundAnchor( lineText, anchorText );

    if ( !windowPhrase.isEmpty() ) {
      return windowPhrase;
    }
  }

  return lineText.trimmed();
}

QString sutraStartupBestLineLookupText( const QString & lineText, const QString & anchorText )
{
  if ( sutraStartupHasLatinLetter( lineText ) || sutraStartupHasLatinLetter( anchorText ) ) {
    return sutraStartupBestVietnamesePhraseFromLine( lineText, anchorText );
  }

  return lineText.trimmed();
}


QString sutraStartupTextFromBstr( BSTR text )
{
  if ( !text ) {
    return {};
  }

  QString result = QString::fromWCharArray( text, static_cast< int >( SysStringLen( text ) ) );
  SysFreeString( text );
  return result;
}

QString sutraStartupTextFromUiAutomationRange( IUIAutomationTextRange * range )
{
  if ( !range ) {
    return {};
  }

  range->ExpandToEnclosingUnit( TextUnit_Line );

  BSTR text = nullptr;
  if ( FAILED( range->GetText( 600, &text ) ) ) {
    return {};
  }

  return sutraStartupTextFromBstr( text ).trimmed();
}

QString sutraStartupUiAutomationTextAtPoint( const QPoint & globalPos )
{
  HRESULT coInitResult = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
  const bool shouldUninitializeCom = SUCCEEDED( coInitResult );

  IUIAutomation * automation = nullptr;
  HRESULT hr = CoCreateInstance( CLSID_CUIAutomation,
                                 nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS( &automation ) );

  if ( FAILED( hr ) || !automation ) {
    if ( shouldUninitializeCom ) {
      CoUninitialize();
    }

    return {};
  }

  POINT point;
  point.x = globalPos.x();
  point.y = globalPos.y();

  IUIAutomationElement * element = nullptr;
  hr = automation->ElementFromPoint( point, &element );

  if ( FAILED( hr ) || !element ) {
    automation->Release();

    if ( shouldUninitializeCom ) {
      CoUninitialize();
    }

    return {};
  }

  IUIAutomationTreeWalker * walker = nullptr;
  automation->get_ControlViewWalker( &walker );

  QString result;
  IUIAutomationElement * currentElement = element;

  for ( int depth = 0; currentElement && depth < 8 && result.trimmed().isEmpty(); ++depth ) {
    IUIAutomationTextPattern * textPattern = nullptr;

    hr = currentElement->GetCurrentPatternAs( UIA_TextPatternId,
                                              IID_PPV_ARGS( &textPattern ) );

    if ( SUCCEEDED( hr ) && textPattern ) {
      IUIAutomationTextRange * textRange = nullptr;
      hr = textPattern->RangeFromPoint( point, &textRange );

      if ( SUCCEEDED( hr ) && textRange ) {
        result = sutraStartupTextFromUiAutomationRange( textRange );
        textRange->Release();
      }

      textPattern->Release();
    }

    if ( !result.trimmed().isEmpty() || !walker ) {
      break;
    }

    IUIAutomationElement * parentElement = nullptr;
    hr = walker->GetParentElement( currentElement, &parentElement );

    if ( FAILED( hr ) || !parentElement ) {
      break;
    }

    if ( currentElement != element ) {
      currentElement->Release();
    }

    currentElement = parentElement;
  }

  if ( currentElement && currentElement != element ) {
    currentElement->Release();
  }

  if ( walker ) {
    walker->Release();
  }

  element->Release();
  automation->Release();

  if ( shouldUninitializeCom ) {
    CoUninitialize();
  }

  return result.trimmed();
}
QString sutraStartupCleanLookupText( const QString & text )
{
  QString result = text.trimmed();
  result.replace( QChar( 0x3000 ), QLatin1Char( ' ' ) );
  result.replace( QLatin1Char( '\r' ), QLatin1Char( '\n' ) );

  while ( result.contains( QStringLiteral( "\n\n" ) ) ) {
    result.replace( QStringLiteral( "\n\n" ), QStringLiteral( "\n" ) );
  }

  if ( result.size() <= 220 ) {
    return result;
  }

  int firstCjk = -1;
  for ( int i = 0; i < result.size(); ++i ) {
    if ( sutraStartupIsCjkChar( result.at( i ) ) ) {
      firstCjk = i;
      break;
    }
  }

  if ( firstCjk < 0 ) {
    return result.left( 220 ).trimmed();
  }

  const int start = qMax( 0, firstCjk - 60 );
  return result.mid( start, 220 ).trimmed();
}

class SutraStartupMouseLookupHook final : public QObject
{
public:
  explicit SutraStartupMouseLookupHook( QObject * parent = nullptr ):
    QObject( parent )
  {
  }

  bool ensureInstalled()
  {
    if ( hook ) {
      return true;
    }

    instance = this;
    hook = SetWindowsHookExW( WH_MOUSE_LL, &SutraStartupMouseLookupHook::mouseProc, GetModuleHandleW( nullptr ), 0 );

    if ( !hook ) {
      qWarning() << "Unable to install Sutra startup mouse lookup hook. Error:" << GetLastError();
      return false;
    }

    qInfo() << "Sutra startup mouse lookup hook installed";
    return true;
  }

  ~SutraStartupMouseLookupHook() override
  {
    if ( hook ) {
      UnhookWindowsHookEx( hook );
      hook = nullptr;
    }

    if ( instance == this ) {
      instance = nullptr;
    }
  }

private:
  static bool isControlPressed()
  {
    return ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 )
        || ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 )
        || ( GetAsyncKeyState( VK_RCONTROL ) & 0x8000 );
  }

  static bool isAltPressed()
  {
    return ( GetAsyncKeyState( VK_MENU ) & 0x8000 )
        || ( GetAsyncKeyState( VK_LMENU ) & 0x8000 )
        || ( GetAsyncKeyState( VK_RMENU ) & 0x8000 );
  }

  static bool shouldHandleMouseLookupEvent( SutraStartupMouseLookupMode mode, WPARAM wParam )
  {
    switch ( mode ) {
      case SutraStartupMouseLookupMode::CtrlRightClick:
        return isControlPressed() && ( wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP );
      case SutraStartupMouseLookupMode::CtrlLeftClick:
        return isControlPressed() && ( wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP );
      case SutraStartupMouseLookupMode::AltRightClick:
        return isAltPressed() && ( wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP );
      case SutraStartupMouseLookupMode::Disabled:
      default:
        return false;
    }
  }

  static bool isMouseLookupDownEvent( SutraStartupMouseLookupMode mode, WPARAM wParam )
  {
    switch ( mode ) {
      case SutraStartupMouseLookupMode::CtrlLeftClick:
        return wParam == WM_LBUTTONDOWN;
      case SutraStartupMouseLookupMode::AltRightClick:
      case SutraStartupMouseLookupMode::CtrlRightClick:
        return wParam == WM_RBUTTONDOWN;
      case SutraStartupMouseLookupMode::Disabled:
      default:
        return false;
    }
  }

  static LRESULT CALLBACK mouseProc( int code, WPARAM wParam, LPARAM lParam )
  {
    if ( code == HC_ACTION && instance ) {
      const SutraStartupMouseLookupMode mode = loadSutraStartupMouseLookupMode();

      if ( shouldHandleMouseLookupEvent( mode, wParam ) ) {
        if ( isMouseLookupDownEvent( mode, wParam ) && !instance->lookupInProgress ) {
          const MSLLHOOKSTRUCT * mouseInfo = reinterpret_cast< const MSLLHOOKSTRUCT * >( lParam );
          const QPoint globalPos( mouseInfo->pt.x, mouseInfo->pt.y );

          QTimer::singleShot( 0, instance, [ globalPos ] {
            if ( instance ) {
              instance->lookupAt( globalPos );
            }
          } );
        }

        return 1;
      }
    }

    return CallNextHookEx( instance ? instance->hook : nullptr, code, wParam, lParam );
  }

  void openLookup( const QString & rawText )
  {
    const QString lookupText = sutraStartupCleanLookupText( rawText );

    if ( lookupText.isEmpty() ) {
      return;
    }

    QProcess::startDetached( QCoreApplication::applicationFilePath(),
                              QStringList() << QStringLiteral( "--scanpopup" ) << lookupText );
  }

  void lookupAt( const QPoint & globalPos )
  {
    lookupInProgress = true;

    const QString contextText = sutraStartupUiAutomationTextAtPoint( globalPos );

    if ( !contextText.trimmed().isEmpty()
      && !sutraStartupShouldUseLineFallbackForPhrase( contextText ) ) {
      openLookup( sutraStartupBestLineLookupText( contextText, QString() ) );
      lookupInProgress = false;
      return;
    }

    QClipboard * clipboard = QApplication::clipboard();

    if ( !clipboard ) {
      lookupInProgress = false;
      return;
    }

    const QString previousClipboardText = clipboard->text( QClipboard::Clipboard );

    releaseSutraStartupControlKeys();
    releaseSutraStartupAltKeys();
    sendSutraStartupLeftDoubleClickAt( globalPos );

    QTimer::singleShot( 140, this, [ this, previousClipboardText, globalPos ] {
      QClipboard * clipboard = QApplication::clipboard();

      if ( !clipboard ) {
        lookupInProgress = false;
        return;
      }

      clipboard->clear( QClipboard::Clipboard );
      sendSutraStartupCtrlC();

      QTimer::singleShot( 220, this, [ this, previousClipboardText, globalPos ] {
        QClipboard * clipboard = QApplication::clipboard();

        if ( !clipboard ) {
          lookupInProgress = false;
          return;
        }

        const QString capturedText = clipboard->text( QClipboard::Clipboard );

        if ( !sutraStartupShouldUseLineFallbackForPhrase( capturedText ) ) {
          if ( !capturedText.trimmed().isEmpty() ) {
            openLookup( capturedText );
          }

          clipboard->setText( previousClipboardText, QClipboard::Clipboard );
          lookupInProgress = false;
          return;
        }

        sendSutraStartupLineSelectionAt( globalPos );

        QTimer::singleShot( 120, this, [ this, previousClipboardText, capturedText ] {
          QClipboard * clipboard = QApplication::clipboard();

          if ( !clipboard ) {
            lookupInProgress = false;
            return;
          }

          clipboard->clear( QClipboard::Clipboard );
          sendSutraStartupCtrlC();

          QTimer::singleShot( 220, this, [ this, previousClipboardText, capturedText ] {
            QClipboard * clipboard = QApplication::clipboard();

            if ( !clipboard ) {
              lookupInProgress = false;
              return;
            }

            const QString lineText = clipboard->text( QClipboard::Clipboard ).trimmed();

            if ( !lineText.isEmpty() && lineText != capturedText.trimmed() ) {
              openLookup( sutraStartupBestLineLookupText( lineText, capturedText ) );
            }
            else if ( !capturedText.trimmed().isEmpty() ) {
              openLookup( capturedText );
            }

            clipboard->setText( previousClipboardText, QClipboard::Clipboard );
            lookupInProgress = false;
          } );
        } );
      } );
    } );
  }

  HHOOK hook = nullptr;
  bool lookupInProgress = false;

  static SutraStartupMouseLookupHook * instance;
};

SutraStartupMouseLookupHook * SutraStartupMouseLookupHook::instance = nullptr;

} // namespace

#endif

int main( int argc, char ** argv )
{
#if defined( WITH_X11 )
  // Platform selection: Higher priority to user intention
  // 1. Respect QT_QPA_PLATFORM if already set.
  // 2. GOLDENDICT_FORCE_XCB forces Xcb (fallback mode).
  // 3. GOLDENDICT_FORCE_WAYLAND forces native Wayland.
  // 4. By default, we let Qt decide (usually native Wayland on Wayland sessions).
  //    This improves HiDPI support but might affect some X11-specific features.

  if ( qEnvironmentVariableIsSet( "GOLDENDICT_FORCE_XCB" ) ) {
    setenv( "QT_QPA_PLATFORM", "xcb", 1 );
  }
  else if ( qEnvironmentVariableIsSet( "GOLDENDICT_FORCE_WAYLAND" ) ) {
    setenv( "QT_QPA_PLATFORM", "wayland", 1 );
  }
#endif

#ifdef Q_OS_MAC
  setenv( "LANG", "en_US.UTF-8", 1 );
#endif

#ifdef Q_OS_WIN32
  // attach the new console to this application's process
  if ( AttachConsole( ATTACH_PARENT_PROCESS ) ) {
    // reopen the std I/O streams to redirect I/O to the new console
    auto ret1 = freopen( "CON", "w", stdout );
    auto ret2 = freopen( "CON", "w", stderr );
    if ( ret1 == nullptr || ret2 == nullptr ) {
      qDebug() << "Attaching console stdout or stderr failed";
    }
  }

  qputenv( "QT_QPA_PLATFORM", "windows:darkmode=1" );

#endif
  // High DPI screen support
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy( Qt::HighDpiScaleFactorRoundingPolicy::PassThrough );
  
  // Registration of custom URL schemes must be done before QCoreApplication/QApplication is created.
  const QStringList localSchemes =
    { "gdlookup", "gdau", "gico", "qrcx", "bres", "bword", "gdprg", "gdvideo", "gdtts", "gdinternal", "entry" };

  for ( const auto & localScheme : localSchemes ) {
    QWebEngineUrlScheme webUiScheme( localScheme.toLatin1() );
    webUiScheme.setSyntax( QWebEngineUrlScheme::Syntax::Host );
    webUiScheme.setFlags( QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::CorsEnabled
                          | QWebEngineUrlScheme::SecureScheme );
    QWebEngineUrlScheme::registerScheme( webUiScheme );
  }

  GD_QApplication app( "GoldenDict-ng", argc, argv );

  app.setDesktopFileName( "io.github.xiaoyifang.goldendict_ng" );
  GD_QApplication::setApplicationName( "GoldenDict-ng" );
  GD_QApplication::setOrganizationDomain( "xiaoyifang.github.io" );
#ifndef Q_OS_MACOS
  // macOS icon is defined in Info.plist
  GD_QApplication::setWindowIcon( QIcon( ":/icons/programicon.png" ) );
#endif

#if defined( USE_BREAKPAD )
  QString appDirPath = Config::getConfigDir() + "crash";

  QDir dir;
  if ( !dir.exists( appDirPath ) ) {
    dir.mkpath( appDirPath );
  }
  #ifdef Q_OS_WIN32

  google_breakpad::ExceptionHandler eh( appDirPath.toStdWString(),
                                        NULL,
                                        callback,
                                        NULL,
                                        google_breakpad::ExceptionHandler::HANDLER_ALL );
  #elif defined( Q_OS_MAC )

  google_breakpad::ExceptionHandler eh( appDirPath.toStdString(), 0, callback, 0, true, NULL );

  #endif
#endif

  GDOptions gdcl{};

  if ( argc > 1 ) {
    processCommandLine( &app, &gdcl );
  }

#ifdef Q_OS_WIN

#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>
  // Under Windows, increase the amount of fopen()-able file descriptors from
  // the default 512 up to 8192.
  _setmaxstdio( 8192 );

#endif

  QFont f = QApplication::font();
  f.setStyleStrategy( QFont::PreferAntialias );
  QApplication::setFont( f );

  if ( app.isRunning() ) {
    bool wasMessage = false;

    // Combine messages into a single structured message
    if ( gdcl.needTranslateWord() ) {
      QString message = "action:translate";
      if ( !gdcl.window.isEmpty() ) {
        message += "|window:" + gdcl.window;
      }
      if ( gdcl.needSetGroup() ) {
        message += "|group:" + gdcl.getGroupName();
      }
      if ( gdcl.needSetPopupGroup() ) {
        message += "|popupGroup:" + gdcl.getPopupGroupName();
      }
      QString encodedWord = QUrl::toPercentEncoding( gdcl.wordToTranslate() );
      message += "|word:" + encodedWord;
      app.sendMessage( message );
      wasMessage = true;
    }
    else if ( gdcl.needSetGroup() ) {
      app.sendMessage( QString( "setGroup: " ) + gdcl.getGroupName() );
      wasMessage = true;
    }
    else if ( gdcl.needSetPopupGroup() ) {
      app.sendMessage( QString( "setPopupGroup: " ) + gdcl.getPopupGroupName() );
      wasMessage = true;
    }
    else if ( gdcl.needTogglePopup() ) {
      app.sendMessage( QStringLiteral( "toggleScanPopup" ) );
      wasMessage = true;
    }
    else if ( !gdcl.window.isEmpty() ) {
      app.sendMessage( QString( "window:" ) + gdcl.window );
      wasMessage = true;
    }

    if ( !wasMessage ) {
      app.sendMessage( "bringToFront" );
    }

    return 0; // Another instance is running
  }

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  // OpenCC needs to load it's data files by relative path on Windows and OS X
  QDir::setCurrent( Config::getProgramDataDir() );
#endif

  Config::Class cfg;
  for ( ;; ) {
    try {
      cfg = Config::load();
    }
    catch ( Config::exError & ) {
      QMessageBox mb(
        QMessageBox::Warning,
        GD_QApplication::applicationName(),
        GD_QApplication::translate( "Main", "Error in configuration file. Continue with default settings?" ),
        QMessageBox::Yes | QMessageBox::No );
      mb.exec();
      if ( mb.result() != QMessageBox::Yes ) {
        return -1;
      }

      QString configFile = Config::getConfigFileName();
      QFile::rename( configFile,
                     configFile % QStringLiteral( "." )
                       % QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss" ) )
                       % QStringLiteral( ".bad" ) );
      continue;
    }
    break;
  }

  if ( gdcl.notts ) {
    cfg.notts = true;
#ifdef TTS_SUPPORT
    cfg.voiceEngines.clear();
#endif
  }

  cfg.resetState = gdcl.resetState;

  // Log to file enabled through command line or preference
  Logger::switchLoggingMethod( gdcl.logFile || cfg.preferences.enableApplicationLog );

  //System Font
  auto font = QApplication::font();
  if ( cfg.preferences.enableInterfaceFont && !cfg.preferences.interfaceFont.isEmpty()
       && font.family() != cfg.preferences.interfaceFont ) {
    font.setFamily( cfg.preferences.interfaceFont );
    QApplication::setFont( font );
  }

  //system font size
  if ( cfg.preferences.enableInterfaceFont && cfg.preferences.interfaceFontSize >= 8
       && cfg.preferences.interfaceFontSize <= 32 ) {
    font.setPixelSize( cfg.preferences.interfaceFontSize );
    QApplication::setFont( font );
  }
  else {
    qDebug() << "Use default font";
    cfg.preferences.interfaceFontSize = Config::DEFAULT_FONT_SIZE;
  }

  // Update default locale
  if ( !cfg.preferences.interfaceLanguage.isEmpty() && QLocale().name() != cfg.preferences.interfaceLanguage ) {
    QLocale::setDefault( QLocale( cfg.preferences.interfaceLanguage ) );
  }
  QApplication::setLayoutDirection( QLocale().textDirection() );

  { /// Translations
    auto loadTranslation_qlocale = []( QTranslator & qtranslator,
                                       const QString & filename,
                                       const QString & prefix,
                                       const QString & directory ) -> bool {
      if ( qtranslator.load( QLocale(), filename, prefix, directory ) ) {
        qDebug() << "TS found: " << qtranslator.filePath();
        return true;
      }
      else {
        qDebug() << "TS failed to load: " << QLocale().uiLanguages() << filename << prefix << " from " << directory;
        return false;
      }
    };

    auto * gd_ts        = new QTranslator( &app );
    auto * qt_ts        = new QTranslator( &app );
    auto * webengine_ts = new QTranslator( &app );

    // For GD's translations,
    // If interfaceLanguage is explicitly set, uses filename-based loading, because GD have more languages than Qt & its locale database.
    // If not, then let Qt's qlocale mechanism decide which one to use, because "locale" handling is different in all 3 platforms, and we don't want to deal with that.

    bool loaded = false;
    if ( cfg.preferences.interfaceLanguage.isEmpty() ) {
      loaded = loadTranslation_qlocale( *gd_ts, QString(), QString(), Config::getLocDir() );
    }
    else if ( cfg.preferences.interfaceLanguage != "en" ) {
      loaded = gd_ts->load( cfg.preferences.interfaceLanguage, Config::getLocDir() );
    }

    // Only install translator if loading succeeds
    if ( loaded ) {
      QCoreApplication::installTranslator( gd_ts );
      qDebug() << "TS found: " << gd_ts->filePath();

      // For macOS bundle, the QLibraryInfo::TranslationsPath is overriden by GD.app/Contents/Resources/qt.conf

      // For Windows, windeployqt will combine multiple qt modules translations into `qt_*` thus no `qtwebengine_*` exists
      // qtwebengine loading will fail on Windows.

      if ( loadTranslation_qlocale( *qt_ts, "qt", "_", QLibraryInfo::path( QLibraryInfo::TranslationsPath ) )
           && qt_ts->language().startsWith( gd_ts->language().first( 2 ) ) ) { // Don't delete this sanity check.
        QCoreApplication::installTranslator( qt_ts );
      }

      if ( loadTranslation_qlocale( *webengine_ts,
                                    "qtwebengine",
                                    "_",
                                    QLibraryInfo::path( QLibraryInfo::TranslationsPath ) )
           && webengine_ts->language().startsWith( gd_ts->language().first( 2 ) ) ) {
        QCoreApplication::installTranslator( webengine_ts );
      }
    }
    else {
      qDebug() << "Goldendict translations not loaded.";
    }
  }

  // Prevent app from quitting spontaneously when it works with popup
  // and with the main window closed.
  app.setQuitOnLastWindowClosed( false );

#ifdef Q_OS_WIN
#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <QRegularExpression>
#include <QStringList>
  SutraStartupMouseLookupHook sutraStartupMouseLookupHook( &app );
  sutraStartupMouseLookupHook.ensureInstalled();
#endif

  MainWindow m( cfg );

  /// Session manager things.
  // Redirect commit data request to Mainwindow's handler.
  QObject::connect(
    &app,
    &QGuiApplication::commitDataRequest,
    &m,
    [ &m ]( QSessionManager & ) {
      m.commitData();
    },
    Qt::DirectConnection );

  // Just don't restart. This probably isn't really needed.
  QObject::connect(
    &app,
    &QGuiApplication::saveStateRequest,
    &app,
    []( QSessionManager & mgr ) {
      mgr.setRestartHint( QSessionManager::RestartNever );
    },
    Qt::DirectConnection );

  QObject::connect( &app, &QtSingleApplication::messageReceived, &m, &MainWindow::messageFromAnotherInstanceReceived );

#ifdef Q_OS_MACOS
  auto macUrlHandler = std::make_unique< MacUrlHandler >( &m );
  QDesktopServices::setUrlHandler( "goldendict", macUrlHandler.get(), "processURL" );
  QObject::connect( macUrlHandler.get(),
                    &MacUrlHandler::wordReceived,
                    &m,
                    &MainWindow::messageFromAnotherInstanceReceived );
#endif

  if ( gdcl.needSetGroup() ) {
    m.setGroupByName( gdcl.getGroupName(), true );
  }

  if ( gdcl.needSetPopupGroup() ) {
    m.setGroupByName( gdcl.getPopupGroupName(), false );
  }

  if ( gdcl.needTranslateWord() ) {
    if ( gdcl.window == "popup" ) {
      m.showTranslation( gdcl.wordToTranslate(), "popup" );
    }
    else {
      // Default to main window when target parameter is not specified or set to "main"
      m.showTranslation( gdcl.wordToTranslate(), "main" );
    }
  }

#ifdef Q_OS_UNIX
  // handle Unix's shutdown signals for graceful exit
  KSignalHandler::self()->watchSignal( SIGINT );
  KSignalHandler::self()->watchSignal( SIGTERM );
  QObject::connect( KSignalHandler::self(), &KSignalHandler::signalReceived, &m, &MainWindow::quitApp );
#endif
  int r = app.exec();
  Logger::closeLogFile();

  return r;
}
