/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "config.hh"
#include "logger.hh"
#include "mainwindow.hh"
#include "version.hh"
#include <QClipboard>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QStyle>
#include <QStyleHints>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
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
#include <algorithm>
#include <limits>
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

enum SutraMouseLookupModifierFlag
{
  SutraMouseLookupCtrl  = 0x01,
  SutraMouseLookupAlt   = 0x02,
  SutraMouseLookupShift = 0x04
};

enum class SutraMouseLookupButton
{
  Left   = 1,
  Right  = 2,
  Middle = 3
};

struct SutraMouseLookupSettings
{
  bool enabled = true;
  int modifiers = SutraMouseLookupCtrl;
  SutraMouseLookupButton button = SutraMouseLookupButton::Right;
};

QString sutraMouseLookupLegacyModeSettingsKey()
{
  return QStringLiteral( "SutraEdition/MouseLookupMode" );
}

QString sutraMouseLookupEnabledSettingsKey()
{
  return QStringLiteral( "SutraEdition/MouseLookupEnabled" );
}

QString sutraMouseLookupModifiersSettingsKey()
{
  return QStringLiteral( "SutraEdition/MouseLookupModifiers" );
}

QString sutraMouseLookupButtonSettingsKey()
{
  return QStringLiteral( "SutraEdition/MouseLookupButton" );
}

void saveSutraMouseLookupSettings( const SutraMouseLookupSettings & value )
{
  QSettings settings;
  settings.setValue( sutraMouseLookupEnabledSettingsKey(), value.enabled );
  settings.setValue( sutraMouseLookupModifiersSettingsKey(), value.modifiers );
  settings.setValue( sutraMouseLookupButtonSettingsKey(), static_cast< int >( value.button ) );

  // Keep the old popup preset setting compatible with earlier Sutra builds.
  if ( !value.enabled ) {
    settings.setValue( sutraMouseLookupLegacyModeSettingsKey(), 0 );
  }
  else if ( value.modifiers == SutraMouseLookupCtrl && value.button == SutraMouseLookupButton::Right ) {
    settings.setValue( sutraMouseLookupLegacyModeSettingsKey(), 1 );
  }
  else if ( value.modifiers == SutraMouseLookupCtrl && value.button == SutraMouseLookupButton::Left ) {
    settings.setValue( sutraMouseLookupLegacyModeSettingsKey(), 2 );
  }
  else if ( value.modifiers == SutraMouseLookupAlt && value.button == SutraMouseLookupButton::Right ) {
    settings.setValue( sutraMouseLookupLegacyModeSettingsKey(), 3 );
  }
  else {
    settings.remove( sutraMouseLookupLegacyModeSettingsKey() );
  }
}

SutraMouseLookupSettings loadSutraMouseLookupSettings()
{
  QSettings settings;

  if ( !settings.contains( sutraMouseLookupEnabledSettingsKey() ) ) {
    const int legacyMode = qBound( 0, settings.value( sutraMouseLookupLegacyModeSettingsKey(), 1 ).toInt(), 3 );

    SutraMouseLookupSettings migrated;
    migrated.enabled = legacyMode != 0;

    switch ( legacyMode ) {
      case 2:
        migrated.modifiers = SutraMouseLookupCtrl;
        migrated.button = SutraMouseLookupButton::Left;
        break;
      case 3:
        migrated.modifiers = SutraMouseLookupAlt;
        migrated.button = SutraMouseLookupButton::Right;
        break;
      case 0:
      case 1:
      default:
        migrated.modifiers = SutraMouseLookupCtrl;
        migrated.button = SutraMouseLookupButton::Right;
        break;
    }

    saveSutraMouseLookupSettings( migrated );
    return migrated;
  }

  SutraMouseLookupSettings result;
  result.enabled = settings.value( sutraMouseLookupEnabledSettingsKey(), true ).toBool();

  const int modifiers = settings.value( sutraMouseLookupModifiersSettingsKey(), SutraMouseLookupCtrl ).toInt();
  result.modifiers = modifiers >= SutraMouseLookupCtrl && modifiers <= ( SutraMouseLookupCtrl | SutraMouseLookupAlt | SutraMouseLookupShift )
                       ? modifiers
                       : SutraMouseLookupCtrl;

  const int button = settings.value( sutraMouseLookupButtonSettingsKey(),
                                     static_cast< int >( SutraMouseLookupButton::Right ) ).toInt();
  result.button = button >= static_cast< int >( SutraMouseLookupButton::Left )
                       && button <= static_cast< int >( SutraMouseLookupButton::Middle )
                    ? static_cast< SutraMouseLookupButton >( button )
                    : SutraMouseLookupButton::Right;

  return result;
}

QString sutraMouseLookupModifiersLabel( int modifiers )
{
  QStringList parts;
  if ( modifiers & SutraMouseLookupCtrl ) {
    parts << QStringLiteral( "Ctrl" );
  }
  if ( modifiers & SutraMouseLookupAlt ) {
    parts << QStringLiteral( "Alt" );
  }
  if ( modifiers & SutraMouseLookupShift ) {
    parts << QStringLiteral( "Shift" );
  }
  return parts.join( QStringLiteral( "+" ) );
}

QString sutraMouseLookupButtonLabel( SutraMouseLookupButton button )
{
  switch ( button ) {
    case SutraMouseLookupButton::Left:
      return QStringLiteral( "Left Click" );
    case SutraMouseLookupButton::Middle:
      return QStringLiteral( "Middle Click" );
    case SutraMouseLookupButton::Right:
    default:
      return QStringLiteral( "Right Click" );
  }
}

QString sutraMouseLookupSettingsLabel( const SutraMouseLookupSettings & value )
{
  if ( !value.enabled ) {
    return QStringLiteral( "Disabled" );
  }

  return QStringLiteral( "%1 + %2" )
    .arg( sutraMouseLookupModifiersLabel( value.modifiers ), sutraMouseLookupButtonLabel( value.button ) );
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

void releaseSutraStartupShiftKeys()
{
  sendSutraStartupVirtualKey( VK_SHIFT, false );
  sendSutraStartupVirtualKey( VK_LSHIFT, false );
  sendSutraStartupVirtualKey( VK_RSHIFT, false );
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

struct SutraStartupTextToken
{
  int start = 0;
  int end   = 0;
  QString normalized;
  bool hasLatin = false;
};

QList< SutraStartupTextToken > sutraStartupTokenizePhraseText( const QString & text )
{
  QList< SutraStartupTextToken > tokens;
  int tokenStart = -1;
  bool tokenHasLatin = false;

  const auto flushToken = [ & ]( int tokenEnd ) {
    if ( tokenStart < 0 || tokenEnd <= tokenStart ) {
      tokenStart = -1;
      tokenHasLatin = false;
      return;
    }

    const QString original = text.mid( tokenStart, tokenEnd - tokenStart );
    const QString normalized = sutraStartupNormalizeVietnameseLookupText( original );

    if ( !normalized.isEmpty() ) {
      tokens.push_back( SutraStartupTextToken{ tokenStart, tokenEnd, normalized, tokenHasLatin } );
    }

    tokenStart = -1;
    tokenHasLatin = false;
  };

  for ( int i = 0; i < text.size(); ++i ) {
    const QChar ch = text.at( i );
    const QChar::Category category = ch.category();
    const bool combiningMark = category == QChar::Mark_NonSpacing
                            || category == QChar::Mark_SpacingCombining
                            || category == QChar::Mark_Enclosing;
    const bool wordCharacter = ch.isLetterOrNumber() || ( combiningMark && tokenStart >= 0 );

    if ( wordCharacter ) {
      if ( tokenStart < 0 ) {
        tokenStart = i;
      }

      if ( ch.isLetter() && ch.unicode() < 0x024F ) {
        tokenHasLatin = true;
      }
    }
    else {
      flushToken( i );
    }
  }

  flushToken( text.size() );
  return tokens;
}

struct SutraStartupVietnameseAlias
{
  QStringList normalizedTokens;
};

const QList< SutraStartupVietnameseAlias > & sutraStartupVietnameseGlossaryAliases()
{
  static const QList< SutraStartupVietnameseAlias > aliases = [] {
    QList< SutraStartupVietnameseAlias > result;
    QStringList seen;

    QFile file( QCoreApplication::applicationDirPath() + QStringLiteral( "/buddhist_terms.json" ) );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
      return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError ) {
      return result;
    }

    const QJsonArray terms = document.isArray()
                           ? document.array()
                           : document.object().value( QStringLiteral( "terms" ) ).toArray();

    const auto appendAlias = [ &result, &seen ]( const QString & rawAlias ) {
      const QList< SutraStartupTextToken > aliasTokens = sutraStartupTokenizePhraseText( rawAlias );
      if ( aliasTokens.size() < 2 || aliasTokens.size() > 10 ) {
        return;
      }

      QStringList normalizedTokens;
      bool hasLatin = false;
      for ( const SutraStartupTextToken & token : aliasTokens ) {
        normalizedTokens << token.normalized;
        hasLatin = hasLatin || token.hasLatin;
      }

      if ( !hasLatin ) {
        return;
      }

      const QString key = normalizedTokens.join( QLatin1Char( ' ' ) );
      if ( key.size() < 4 || key.size() > 120 || seen.contains( key ) ) {
        return;
      }

      seen << key;
      result.push_back( SutraStartupVietnameseAlias{ normalizedTokens } );
    };

    const auto appendJsonValue = [ &appendAlias ]( const QJsonValue & value ) {
      if ( value.isString() ) {
        appendAlias( value.toString() );
      }
      else if ( value.isArray() ) {
        for ( const QJsonValue & item : value.toArray() ) {
          if ( item.isString() ) {
            appendAlias( item.toString() );
          }
        }
      }
    };

    for ( const QJsonValue & termValue : terms ) {
      if ( !termValue.isObject() ) {
        continue;
      }

      const QJsonObject object = termValue.toObject();
      appendJsonValue( object.value( QStringLiteral( "han_viet" ) ) );
      appendJsonValue( object.value( QStringLiteral( "suggested_translation" ) ) );
      appendJsonValue( object.value( QStringLiteral( "suggested_translations" ) ) );
    }

    std::sort( result.begin(), result.end(), []( const SutraStartupVietnameseAlias & left,
                                                 const SutraStartupVietnameseAlias & right ) {
      if ( left.normalizedTokens.size() != right.normalizedTokens.size() ) {
        return left.normalizedTokens.size() > right.normalizedTokens.size();
      }

      return left.normalizedTokens.join( QLatin1Char( ' ' ) ).size()
           > right.normalizedTokens.join( QLatin1Char( ' ' ) ).size();
    } );

    return result;
  }();

  return aliases;
}

int sutraStartupTokenIndexAtOffset( const QList< SutraStartupTextToken > & tokens, int offset )
{
  if ( tokens.isEmpty() ) {
    return -1;
  }

  for ( int i = 0; i < tokens.size(); ++i ) {
    if ( offset >= tokens.at( i ).start && offset < tokens.at( i ).end ) {
      return i;
    }
  }

  int nearestIndex = 0;
  int nearestDistance = ( std::numeric_limits< int >::max )();

  for ( int i = 0; i < tokens.size(); ++i ) {
    const int distance = offset < tokens.at( i ).start
                       ? tokens.at( i ).start - offset
                       : offset - tokens.at( i ).end;
    if ( distance < nearestDistance ) {
      nearestDistance = distance;
      nearestIndex = i;
    }
  }

  return nearestIndex;
}

bool sutraStartupTokenSequenceMatches( const QList< SutraStartupTextToken > & lineTokens,
                                       int start,
                                       const QStringList & aliasTokens )
{
  if ( start < 0 || start + aliasTokens.size() > lineTokens.size() ) {
    return false;
  }

  for ( int i = 0; i < aliasTokens.size(); ++i ) {
    if ( lineTokens.at( start + i ).normalized != aliasTokens.at( i ) ) {
      return false;
    }
  }

  return true;
}

QString sutraStartupFallbackVietnameseWindow( const QString & lineText,
                                               const QList< SutraStartupTextToken > & tokens,
                                               int anchorIndex )
{
  if ( anchorIndex < 0 || anchorIndex >= tokens.size() || !tokens.at( anchorIndex ).hasLatin ) {
    return {};
  }

  int clauseStart = anchorIndex;
  int clauseEnd = anchorIndex;

  const auto containsStrongSeparator = [ &lineText ]( int from, int to ) {
    for ( int i = qMax( 0, from ); i < qMin( to, lineText.size() ); ++i ) {
      const QChar ch = lineText.at( i );
      if ( ch == QLatin1Char( ',' ) || ch == QLatin1Char( '.' ) || ch == QLatin1Char( ';' )
        || ch == QLatin1Char( ':' ) || ch == QLatin1Char( '!' ) || ch == QLatin1Char( '?' )
        || ch == QLatin1Char( '\r' ) || ch == QLatin1Char( '\n' )
        || ch == QChar( 0x2013 ) || ch == QChar( 0x2014 ) ) {
        return true;
      }
    }

    return false;
  };

  while ( clauseStart > 0
       && !containsStrongSeparator( tokens.at( clauseStart - 1 ).end, tokens.at( clauseStart ).start ) ) {
    --clauseStart;
  }

  while ( clauseEnd + 1 < tokens.size()
       && !containsStrongSeparator( tokens.at( clauseEnd ).end, tokens.at( clauseEnd + 1 ).start ) ) {
    ++clauseEnd;
  }

  const int clauseTokenCount = clauseEnd - clauseStart + 1;
  if ( clauseTokenCount <= 4 ) {
    return lineText.mid( tokens.at( clauseStart ).start,
                         tokens.at( clauseEnd ).end - tokens.at( clauseStart ).start ).trimmed();
  }

  int windowStart = qMax( clauseStart, anchorIndex - 1 );
  int windowEnd = qMin( clauseEnd, windowStart + 3 );
  if ( windowEnd - windowStart < 3 ) {
    windowStart = qMax( clauseStart, windowEnd - 3 );
  }

  return lineText.mid( tokens.at( windowStart ).start,
                       tokens.at( windowEnd ).end - tokens.at( windowStart ).start ).trimmed();
}

QString sutraStartupBestVietnamesePhraseAtToken( const QString & lineText,
                                                  const QList< SutraStartupTextToken > & lineTokens,
                                                  int anchorIndex )
{
  if ( anchorIndex < 0 || anchorIndex >= lineTokens.size() || !lineTokens.at( anchorIndex ).hasLatin ) {
    return {};
  }

  int bestStart = -1;
  int bestEnd = -1;
  int bestTokenCount = 0;
  int bestCharacterCount = 0;

  for ( const SutraStartupVietnameseAlias & alias : sutraStartupVietnameseGlossaryAliases() ) {
    const int aliasTokenCount = alias.normalizedTokens.size();
    if ( aliasTokenCount < bestTokenCount || aliasTokenCount > lineTokens.size() ) {
      continue;
    }

    const int firstPossibleStart = qMax( 0, anchorIndex - aliasTokenCount + 1 );
    const int lastPossibleStart = qMin( anchorIndex, lineTokens.size() - aliasTokenCount );

    for ( int start = firstPossibleStart; start <= lastPossibleStart; ++start ) {
      if ( !sutraStartupTokenSequenceMatches( lineTokens, start, alias.normalizedTokens ) ) {
        continue;
      }

      const int end = start + aliasTokenCount - 1;
      const int characterCount = lineTokens.at( end ).end - lineTokens.at( start ).start;
      if ( aliasTokenCount > bestTokenCount
        || ( aliasTokenCount == bestTokenCount && characterCount > bestCharacterCount ) ) {
        bestStart = start;
        bestEnd = end;
        bestTokenCount = aliasTokenCount;
        bestCharacterCount = characterCount;
      }
    }
  }

  if ( bestStart >= 0 && bestEnd >= bestStart ) {
    return lineText.mid( lineTokens.at( bestStart ).start,
                         lineTokens.at( bestEnd ).end - lineTokens.at( bestStart ).start ).trimmed();
  }

  return sutraStartupFallbackVietnameseWindow( lineText, lineTokens, anchorIndex );
}

QString sutraStartupBestVietnamesePhraseAtOffset( const QString & lineText, int clickOffset )
{
  const QList< SutraStartupTextToken > tokens = sutraStartupTokenizePhraseText( lineText );
  const int anchorIndex = sutraStartupTokenIndexAtOffset( tokens, clickOffset );
  return sutraStartupBestVietnamesePhraseAtToken( lineText, tokens, anchorIndex );
}

QString sutraStartupBestVietnamesePhraseFromLine( const QString & lineText, const QString & anchorText )
{
  const QList< SutraStartupTextToken > lineTokens = sutraStartupTokenizePhraseText( lineText );
  const QList< SutraStartupTextToken > anchorTokens = sutraStartupTokenizePhraseText( anchorText );

  if ( lineTokens.isEmpty() ) {
    return {};
  }

  int anchorIndex = -1;
  if ( !anchorTokens.isEmpty() ) {
    QStringList normalizedAnchorTokens;
    for ( const SutraStartupTextToken & token : anchorTokens ) {
      normalizedAnchorTokens << token.normalized;
    }

    for ( int start = 0; start + normalizedAnchorTokens.size() <= lineTokens.size(); ++start ) {
      if ( sutraStartupTokenSequenceMatches( lineTokens, start, normalizedAnchorTokens ) ) {
        anchorIndex = start + normalizedAnchorTokens.size() / 2;
        break;
      }
    }
  }

  if ( anchorIndex < 0 ) {
    for ( int i = 0; i < lineTokens.size(); ++i ) {
      if ( lineTokens.at( i ).hasLatin ) {
        anchorIndex = i;
        break;
      }
    }
  }

  return sutraStartupBestVietnamesePhraseAtToken( lineText, lineTokens, anchorIndex );
}

QString sutraStartupBestLineLookupText( const QString & lineText, const QString & anchorText )
{
  if ( sutraStartupHasLatinLetter( lineText ) || sutraStartupHasLatinLetter( anchorText ) ) {
    const QString phrase = sutraStartupBestVietnamesePhraseFromLine( lineText, anchorText );
    if ( !phrase.isEmpty() ) {
      return phrase;
    }
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

bool sutraStartupIsCjkPhraseSeparator( const QChar & ch )
{
  switch ( ch.unicode() ) {
    case 0x002C: // ,
    case 0x002E: // .
    case 0x003A: // :
    case 0x003B: // ;
    case 0x003F: // ?
    case 0x0021: // !
    case 0x3001: // IDEOGRAPHIC COMMA
    case 0x3002: // IDEOGRAPHIC FULL STOP
    case 0xFF01: // FULLWIDTH EXCLAMATION MARK
    case 0xFF0C: // FULLWIDTH COMMA
    case 0xFF0E: // FULLWIDTH FULL STOP
    case 0xFF1A: // FULLWIDTH COLON
    case 0xFF1B: // FULLWIDTH SEMICOLON
    case 0xFF1F: // FULLWIDTH QUESTION MARK
      return true;
    default:
      return ch == QLatin1Char( '\r' ) || ch == QLatin1Char( '\n' );
  }
}

QString sutraStartupTextFromUiAutomationRange( IUIAutomationTextRange * range )
{
  if ( !range ) {
    return {};
  }

  // Keep the point range unchanged. Derive the enclosing line and the text
  // from the line start to the clicked point so a CJK clause under the mouse
  // can be selected instead of always returning the first term in the line.
  IUIAutomationTextRange * lineRange = nullptr;
  if ( FAILED( range->Clone( &lineRange ) ) || !lineRange ) {
    return {};
  }

  lineRange->ExpandToEnclosingUnit( TextUnit_Line );

  BSTR lineBstr = nullptr;
  if ( FAILED( lineRange->GetText( 600, &lineBstr ) ) ) {
    lineRange->Release();
    return {};
  }

  const QString lineText = sutraStartupTextFromBstr( lineBstr );
  int clickOffset        = -1;

  IUIAutomationTextRange * prefixRange = nullptr;
  if ( SUCCEEDED( lineRange->Clone( &prefixRange ) ) && prefixRange ) {
    if ( SUCCEEDED( prefixRange->MoveEndpointByRange( TextPatternRangeEndpoint_End,
                                                      range,
                                                      TextPatternRangeEndpoint_Start ) ) ) {
      BSTR prefixBstr = nullptr;
      if ( SUCCEEDED( prefixRange->GetText( 600, &prefixBstr ) ) ) {
        clickOffset = sutraStartupTextFromBstr( prefixBstr ).size();
      }
    }

    prefixRange->Release();
  }

  lineRange->Release();

  if ( clickOffset >= 0 && !lineText.isEmpty() && sutraStartupHasLatinLetter( lineText ) ) {
    const QString vietnamesePhrase = sutraStartupBestVietnamesePhraseAtOffset( lineText, clickOffset );
    if ( !vietnamesePhrase.isEmpty() && vietnamesePhrase.size() <= 120 ) {
      return vietnamesePhrase;
    }
  }

  if ( clickOffset >= 0 && !lineText.isEmpty() ) {
    clickOffset = qBound( 0, clickOffset, lineText.size() );

    int anchor = clickOffset;
    if ( anchor >= lineText.size() ) {
      anchor = lineText.size() - 1;
    }

    if ( anchor >= 0 && sutraStartupIsCjkPhraseSeparator( lineText.at( anchor ) ) ) {
      int right = anchor + 1;
      while ( right < lineText.size()
              && ( sutraStartupIsCjkPhraseSeparator( lineText.at( right ) ) || lineText.at( right ).isSpace() ) ) {
        ++right;
      }

      int left = anchor - 1;
      while ( left >= 0
              && ( sutraStartupIsCjkPhraseSeparator( lineText.at( left ) ) || lineText.at( left ).isSpace() ) ) {
        --left;
      }

      if ( right < lineText.size() ) {
        anchor = right;
      }
      else if ( left >= 0 ) {
        anchor = left;
      }
    }

    if ( anchor >= 0 ) {
      int start = anchor;
      while ( start > 0 && !sutraStartupIsCjkPhraseSeparator( lineText.at( start - 1 ) ) ) {
        --start;
      }

      int end = anchor + 1;
      while ( end < lineText.size() && !sutraStartupIsCjkPhraseSeparator( lineText.at( end ) ) ) {
        ++end;
      }

      const QString clause = lineText.mid( start, end - start ).trimmed();

      bool clauseHasCjk = false;
      for ( const QChar & ch : clause ) {
        if ( sutraStartupIsCjkChar( ch ) ) {
          clauseHasCjk = true;
          break;
        }
      }

      if ( clauseHasCjk && !clause.isEmpty() && clause.size() <= 80 ) {
        return clause;
      }
    }
  }

  // Some controls expose a useful word boundary even when punctuation-based
  // extraction is unavailable.
  IUIAutomationTextRange * wordRange = nullptr;
  if ( SUCCEEDED( range->Clone( &wordRange ) ) && wordRange ) {
    wordRange->ExpandToEnclosingUnit( TextUnit_Word );

    BSTR wordBstr = nullptr;
    if ( SUCCEEDED( wordRange->GetText( 120, &wordBstr ) ) ) {
      const QString wordText = sutraStartupTextFromBstr( wordBstr ).trimmed();
      wordRange->Release();

      if ( !wordText.isEmpty() && wordText.size() <= 80 ) {
        return wordText;
      }
    }
    else {
      wordRange->Release();
    }
  }

  return lineText.trimmed();
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


struct SutraMainThemeBaseline
{
  bool initialized = false;
  QPalette palette;
  QString styleName;
};

SutraMainThemeBaseline & sutraMainThemeBaseline()
{
  static SutraMainThemeBaseline value;
  return value;
}

Config::Dark & sutraCurrentMainThemeMode()
{
  static Config::Dark mode = Config::Dark::Off;
  return mode;
}

void initializeSutraMainThemeBaseline( QApplication * app )
{
  if ( !app ) {
    return;
  }

  SutraMainThemeBaseline & baseline = sutraMainThemeBaseline();
  if ( baseline.initialized ) {
    return;
  }

  baseline.palette = app->palette();
  baseline.styleName = app->style() ? app->style()->objectName() : QString();
  baseline.initialized = true;
}

bool sutraSystemUsesDarkTheme( QApplication * app )
{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
  return app && app->styleHints() && app->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
  Q_UNUSED( app );
  return false;
#endif
}

QPalette sutraMainDarkPalette()
{
  QPalette palette;
  palette.setColor( QPalette::Window, QColor( 30, 32, 36 ) );
  palette.setColor( QPalette::WindowText, QColor( 238, 240, 244 ) );
  palette.setColor( QPalette::Base, QColor( 20, 22, 26 ) );
  palette.setColor( QPalette::AlternateBase, QColor( 39, 42, 48 ) );
  palette.setColor( QPalette::ToolTipBase, QColor( 39, 42, 48 ) );
  palette.setColor( QPalette::ToolTipText, QColor( 248, 249, 250 ) );
  palette.setColor( QPalette::Text, QColor( 238, 240, 244 ) );
  palette.setColor( QPalette::Button, QColor( 45, 48, 55 ) );
  palette.setColor( QPalette::ButtonText, QColor( 238, 240, 244 ) );
  palette.setColor( QPalette::BrightText, QColor( 255, 99, 99 ) );
  palette.setColor( QPalette::Link, QColor( 112, 165, 255 ) );
  palette.setColor( QPalette::Highlight, QColor( 58, 119, 255 ) );
  palette.setColor( QPalette::HighlightedText, QColor( 255, 255, 255 ) );
  palette.setColor( QPalette::PlaceholderText, QColor( 155, 160, 170 ) );
  palette.setColor( QPalette::Disabled, QPalette::Text, QColor( 125, 130, 140 ) );
  palette.setColor( QPalette::Disabled, QPalette::ButtonText, QColor( 125, 130, 140 ) );
  return palette;
}

void applySutraMainApplicationTheme( QApplication * app, Config::Dark mode )
{
  if ( !app ) {
    return;
  }

  initializeSutraMainThemeBaseline( app );
  sutraCurrentMainThemeMode() = mode;

  const bool dark = mode == Config::Dark::On
                 || ( mode == Config::Dark::Auto && sutraSystemUsesDarkTheme( app ) );

  SutraMainThemeBaseline & baseline = sutraMainThemeBaseline();

  if ( dark ) {
#ifdef Q_OS_WIN32
    if ( QStyle * fusion = QStyleFactory::create( QStringLiteral( "Fusion" ) ) ) {
      app->setStyle( fusion );
    }
#endif
    app->setPalette( sutraMainDarkPalette() );
  }
  else {
#ifdef Q_OS_WIN32
    if ( !baseline.styleName.isEmpty() ) {
      if ( QStyle * originalStyle = QStyleFactory::create( baseline.styleName ) ) {
        app->setStyle( originalStyle );
      }
    }
#endif
    app->setPalette( baseline.palette );
  }

  for ( QWidget * widget : QApplication::topLevelWidgets() ) {
    if ( widget ) {
      widget->setProperty( "sutraMainDarkTheme", dark );
      widget->style()->unpolish( widget );
      widget->style()->polish( widget );
      widget->update();
    }
  }
}

class SutraPreferencesEnhancer final : public QObject
{
public:
  explicit SutraPreferencesEnhancer( QApplication * application ):
    QObject( application ),
    app( application )
  {
  }

protected:
  bool eventFilter( QObject * watched, QEvent * event ) override
  {
    if ( event && event->type() == QEvent::Show ) {
      if ( QDialog * dialog = qobject_cast< QDialog * >( watched ) ) {
        enhancePreferencesDialog( dialog );
      }
    }

    return QObject::eventFilter( watched, event );
  }

private:
  static void populateModifierCombo( QComboBox * combo )
  {
    combo->addItem( QStringLiteral( "Ctrl" ), SutraMouseLookupCtrl );
    combo->addItem( QStringLiteral( "Alt" ), SutraMouseLookupAlt );
    combo->addItem( QStringLiteral( "Shift" ), SutraMouseLookupShift );
    combo->addItem( QStringLiteral( "Ctrl + Alt" ), SutraMouseLookupCtrl | SutraMouseLookupAlt );
    combo->addItem( QStringLiteral( "Ctrl + Shift" ), SutraMouseLookupCtrl | SutraMouseLookupShift );
    combo->addItem( QStringLiteral( "Alt + Shift" ), SutraMouseLookupAlt | SutraMouseLookupShift );
    combo->addItem( QStringLiteral( "Ctrl + Alt + Shift" ),
                    SutraMouseLookupCtrl | SutraMouseLookupAlt | SutraMouseLookupShift );
  }

  static void populateButtonCombo( QComboBox * combo )
  {
    combo->addItem( QObject::tr( "Left Click" ), static_cast< int >( SutraMouseLookupButton::Left ) );
    combo->addItem( QObject::tr( "Right Click" ), static_cast< int >( SutraMouseLookupButton::Right ) );
    combo->addItem( QObject::tr( "Middle Click" ), static_cast< int >( SutraMouseLookupButton::Middle ) );
  }

  void enhanceMouseLookupSettings( QDialog * dialog )
  {
    QWidget * hotkeyTab = dialog->findChild< QWidget * >( QStringLiteral( "tab_hotkey" ) );
    if ( !hotkeyTab || hotkeyTab->findChild< QWidget * >( QStringLiteral( "sutraMouseLookupSettingsGroup" ) ) ) {
      return;
    }

    QBoxLayout * tabLayout = qobject_cast< QBoxLayout * >( hotkeyTab->layout() );
    if ( !tabLayout ) {
      tabLayout = new QVBoxLayout( hotkeyTab );
    }

    QGroupBox * group = new QGroupBox( QObject::tr( "Mouse lookup" ), hotkeyTab );
    group->setObjectName( QStringLiteral( "sutraMouseLookupSettingsGroup" ) );
    group->setToolTip( QObject::tr( "Choose the keyboard modifiers and mouse button used to open the lookup popup." ) );

    QVBoxLayout * groupLayout = new QVBoxLayout( group );
    QCheckBox * enabled = new QCheckBox( QObject::tr( "Enable mouse lookup shortcut" ), group );
    enabled->setObjectName( QStringLiteral( "sutraMouseLookupEnabled" ) );
    groupLayout->addWidget( enabled );

    QHBoxLayout * selectorLayout = new QHBoxLayout;
    QLabel * modifierLabel = new QLabel( QObject::tr( "Modifier:" ), group );
    QComboBox * modifierCombo = new QComboBox( group );
    modifierCombo->setObjectName( QStringLiteral( "sutraMouseLookupModifier" ) );
    populateModifierCombo( modifierCombo );

    QLabel * buttonLabel = new QLabel( QObject::tr( "Mouse button:" ), group );
    QComboBox * buttonCombo = new QComboBox( group );
    buttonCombo->setObjectName( QStringLiteral( "sutraMouseLookupButton" ) );
    populateButtonCombo( buttonCombo );

    selectorLayout->addWidget( modifierLabel );
    selectorLayout->addWidget( modifierCombo, 1 );
    selectorLayout->addSpacing( 12 );
    selectorLayout->addWidget( buttonLabel );
    selectorLayout->addWidget( buttonCombo, 1 );
    groupLayout->addLayout( selectorLayout );

    QLabel * currentLabel = new QLabel( group );
    currentLabel->setObjectName( QStringLiteral( "sutraMouseLookupCurrent" ) );
    currentLabel->setWordWrap( true );
    groupLayout->addWidget( currentLabel );

    const SutraMouseLookupSettings current = loadSutraMouseLookupSettings();
    enabled->setChecked( current.enabled );

    int modifierIndex = modifierCombo->findData( current.modifiers );
    modifierCombo->setCurrentIndex( modifierIndex >= 0 ? modifierIndex : 0 );

    int buttonIndex = buttonCombo->findData( static_cast< int >( current.button ) );
    buttonCombo->setCurrentIndex( buttonIndex >= 0 ? buttonIndex : 1 );

    auto updateControls = [ enabled, modifierCombo, buttonCombo, currentLabel ] {
      modifierCombo->setEnabled( enabled->isChecked() );
      buttonCombo->setEnabled( enabled->isChecked() );

      SutraMouseLookupSettings value;
      value.enabled = enabled->isChecked();
      value.modifiers = modifierCombo->currentData().toInt();
      value.button = static_cast< SutraMouseLookupButton >( buttonCombo->currentData().toInt() );
      currentLabel->setText( QObject::tr( "Current combination: %1" ).arg( sutraMouseLookupSettingsLabel( value ) ) );
    };

    QObject::connect( enabled, &QCheckBox::toggled, dialog, [ updateControls ] { updateControls(); } );
    QObject::connect( modifierCombo, &QComboBox::currentIndexChanged, dialog, [ updateControls ] { updateControls(); } );
    QObject::connect( buttonCombo, &QComboBox::currentIndexChanged, dialog, [ updateControls ] { updateControls(); } );
    updateControls();

    const int insertionIndex = qMax( 0, tabLayout->count() - 1 );
    tabLayout->insertWidget( insertionIndex, group );

    if ( QDialogButtonBox * buttons = dialog->findChild< QDialogButtonBox * >( QStringLiteral( "buttonBox" ) ) ) {
      QObject::connect( buttons, &QDialogButtonBox::accepted, dialog,
                        [ enabled, modifierCombo, buttonCombo ] {
        SutraMouseLookupSettings value;
        value.enabled = enabled->isChecked();
        value.modifiers = modifierCombo->currentData().toInt();
        value.button = static_cast< SutraMouseLookupButton >( buttonCombo->currentData().toInt() );
        saveSutraMouseLookupSettings( value );
      } );
    }
  }

  void enhanceMainThemeSettings( QDialog * dialog )
  {
    QComboBox * darkMode = dialog->findChild< QComboBox * >( QStringLiteral( "darkMode" ) );
    if ( !darkMode || darkMode->property( "sutraMainThemeEnhanced" ).toBool() ) {
      return;
    }

    darkMode->setProperty( "sutraMainThemeEnhanced", true );
    darkMode->setToolTip( QObject::tr( "Changes the theme of the main GoldenDict window. The popup theme remains independent." ) );

    if ( QLabel * label = dialog->findChild< QLabel * >( QStringLiteral( "darkModeLabel" ) ) ) {
      label->setText( QObject::tr( "Main application theme:" ) );
      label->setToolTip( darkMode->toolTip() );
    }

    const Config::Dark originalMode = darkMode->currentData().value< Config::Dark >();

    QObject::connect( darkMode, &QComboBox::currentIndexChanged, dialog, [ this, darkMode ] {
      applySutraMainApplicationTheme( app, darkMode->currentData().value< Config::Dark >() );
    } );

    QObject::connect( dialog, &QDialog::rejected, dialog, [ this, originalMode ] {
      applySutraMainApplicationTheme( app, originalMode );
    } );
  }

  void enhancePreferencesDialog( QDialog * dialog )
  {
    if ( !dialog || !dialog->findChild< QTabWidget * >() ) {
      return;
    }

    // Only the GoldenDict Preferences dialog contains both of these controls.
    if ( !dialog->findChild< QWidget * >( QStringLiteral( "tab_hotkey" ) )
      || !dialog->findChild< QDialogButtonBox * >( QStringLiteral( "buttonBox" ) ) ) {
      return;
    }

    enhanceMouseLookupSettings( dialog );
    enhanceMainThemeSettings( dialog );
  }

  QApplication * app = nullptr;
};

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
  static bool keyPressed( int virtualKey )
  {
    return ( GetAsyncKeyState( virtualKey ) & 0x8000 ) != 0;
  }

  static bool modifiersMatch( int expectedModifiers )
  {
    const bool ctrlPressed = keyPressed( VK_CONTROL ) || keyPressed( VK_LCONTROL ) || keyPressed( VK_RCONTROL );
    const bool altPressed = keyPressed( VK_MENU ) || keyPressed( VK_LMENU ) || keyPressed( VK_RMENU );
    const bool shiftPressed = keyPressed( VK_SHIFT ) || keyPressed( VK_LSHIFT ) || keyPressed( VK_RSHIFT );

    return ctrlPressed == bool( expectedModifiers & SutraMouseLookupCtrl )
        && altPressed == bool( expectedModifiers & SutraMouseLookupAlt )
        && shiftPressed == bool( expectedModifiers & SutraMouseLookupShift );
  }

  static bool isConfiguredButtonEvent( SutraMouseLookupButton button, WPARAM wParam )
  {
    switch ( button ) {
      case SutraMouseLookupButton::Left:
        return wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP;
      case SutraMouseLookupButton::Middle:
        return wParam == WM_MBUTTONDOWN || wParam == WM_MBUTTONUP;
      case SutraMouseLookupButton::Right:
      default:
        return wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP;
    }
  }

  static bool isConfiguredButtonDownEvent( SutraMouseLookupButton button, WPARAM wParam )
  {
    switch ( button ) {
      case SutraMouseLookupButton::Left:
        return wParam == WM_LBUTTONDOWN;
      case SutraMouseLookupButton::Middle:
        return wParam == WM_MBUTTONDOWN;
      case SutraMouseLookupButton::Right:
      default:
        return wParam == WM_RBUTTONDOWN;
    }
  }

  static LRESULT CALLBACK mouseProc( int code, WPARAM wParam, LPARAM lParam )
  {
    if ( code == HC_ACTION && instance ) {
      const SutraMouseLookupSettings settings = loadSutraMouseLookupSettings();

      if ( settings.enabled
        && modifiersMatch( settings.modifiers )
        && isConfiguredButtonEvent( settings.button, wParam ) ) {
        if ( isConfiguredButtonDownEvent( settings.button, wParam ) && !instance->lookupInProgress ) {
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
    releaseSutraStartupShiftKeys();
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
#ifdef Q_OS_WIN
  initializeSutraMainThemeBaseline( &app );
#endif
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

#ifdef Q_OS_WIN
  applySutraMainApplicationTheme( &app, cfg.preferences.darkMode );
#endif

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
  SutraPreferencesEnhancer sutraPreferencesEnhancer( &app );
  app.installEventFilter( &sutraPreferencesEnhancer );

#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
  QObject::connect( app.styleHints(), &QStyleHints::colorSchemeChanged, &app, [ &app ]( Qt::ColorScheme ) {
    if ( sutraCurrentMainThemeMode() == Config::Dark::Auto ) {
      applySutraMainApplicationTheme( &app, Config::Dark::Auto );
    }
  } );
#endif

  SutraStartupMouseLookupHook sutraStartupMouseLookupHook( &app );
  sutraStartupMouseLookupHook.ensureInstalled();
#endif

  MainWindow m( cfg );

#ifdef Q_OS_WIN
  // MainWindow construction may apply its own style; enforce the selected main theme once more.
  applySutraMainApplicationTheme( &app, cfg.preferences.darkMode );
#endif

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
