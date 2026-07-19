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
#include <QSet>
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
#include <functional>
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

enum class SutraMouseLookupCaptureMode
{
  Automatic     = 0,
  SelectedText  = 1,
  EntirePhrase  = 2
};

struct SutraMouseLookupSettings
{
  bool enabled = true;
  int modifiers = SutraMouseLookupCtrl;
  SutraMouseLookupButton button = SutraMouseLookupButton::Right;
  SutraMouseLookupCaptureMode captureMode = SutraMouseLookupCaptureMode::Automatic;
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

QString sutraMouseLookupCaptureModeSettingsKey()
{
  return QStringLiteral( "SutraEdition/MouseLookupCaptureMode" );
}

void saveSutraMouseLookupSettings( const SutraMouseLookupSettings & value )
{
  QSettings settings;
  settings.setValue( sutraMouseLookupEnabledSettingsKey(), value.enabled );
  settings.setValue( sutraMouseLookupModifiersSettingsKey(), value.modifiers );
  settings.setValue( sutraMouseLookupButtonSettingsKey(), static_cast< int >( value.button ) );
  settings.setValue( sutraMouseLookupCaptureModeSettingsKey(), static_cast< int >( value.captureMode ) );

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

  const int captureMode = settings.value( sutraMouseLookupCaptureModeSettingsKey(),
                                          static_cast< int >( SutraMouseLookupCaptureMode::Automatic ) ).toInt();
  if ( captureMode == static_cast< int >( SutraMouseLookupCaptureMode::SelectedText ) ) {
    result.captureMode = SutraMouseLookupCaptureMode::SelectedText;
  }
  else if ( captureMode == static_cast< int >( SutraMouseLookupCaptureMode::EntirePhrase ) ) {
    result.captureMode = SutraMouseLookupCaptureMode::EntirePhrase;
  }
  else {
    result.captureMode = SutraMouseLookupCaptureMode::Automatic;
  }

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

QString sutraMouseLookupCaptureModeLabel( SutraMouseLookupCaptureMode mode )
{
  switch ( mode ) {
    case SutraMouseLookupCaptureMode::SelectedText:
      return QStringLiteral( "Selected text only" );
    case SutraMouseLookupCaptureMode::EntirePhrase:
      return QStringLiteral( "Automatic entire phrase" );
    case SutraMouseLookupCaptureMode::Automatic:
    default:
      return QStringLiteral( "Automatic precise phrase" );
  }
}

QString sutraMouseLookupSettingsLabel( const SutraMouseLookupSettings & value )
{
  if ( !value.enabled ) {
    return QStringLiteral( "Disabled" );
  }

  return QStringLiteral( "%1 + %2 - %3" )
    .arg( sutraMouseLookupModifiersLabel( value.modifiers ),
          sutraMouseLookupButtonLabel( value.button ),
          sutraMouseLookupCaptureModeLabel( value.captureMode ) );
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

bool sutraStartupAnyPhysicalModifierDown()
{
  return ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 )
      || ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 )
      || ( GetAsyncKeyState( VK_RCONTROL ) & 0x8000 )
      || ( GetAsyncKeyState( VK_MENU ) & 0x8000 )
      || ( GetAsyncKeyState( VK_LMENU ) & 0x8000 )
      || ( GetAsyncKeyState( VK_RMENU ) & 0x8000 )
      || ( GetAsyncKeyState( VK_SHIFT ) & 0x8000 )
      || ( GetAsyncKeyState( VK_LSHIFT ) & 0x8000 )
      || ( GetAsyncKeyState( VK_RSHIFT ) & 0x8000 );
}

bool sutraStartupIsMicrosoftOfficeWindowAtPoint( const QPoint & globalPos )
{
  POINT point;
  point.x = globalPos.x();
  point.y = globalPos.y();

  HWND window = WindowFromPoint( point );
  if ( !window ) {
    return false;
  }

  HWND rootWindow = GetAncestor( window, GA_ROOT );
  if ( rootWindow ) {
    window = rootWindow;
  }

  wchar_t className[ 256 ] = {};
  const int classLength = GetClassNameW( window, className, static_cast< int >( sizeof( className ) / sizeof( className[ 0 ] ) ) );
  if ( classLength > 0 ) {
    const QString windowClass = QString::fromWCharArray( className, classLength );
    static const QStringList officeWindowClasses = {
      QStringLiteral( "OpusApp" ),
      QStringLiteral( "XLMAIN" ),
      QStringLiteral( "PPTFrameClass" ),
      QStringLiteral( "rctrl_renwnd32" ),
      QStringLiteral( "OneNote::MainFrame" )
    };

    for ( const QString & officeClass : officeWindowClasses ) {
      if ( windowClass.compare( officeClass, Qt::CaseInsensitive ) == 0 ) {
        return true;
      }
    }
  }

  DWORD processId = 0;
  GetWindowThreadProcessId( window, &processId );
  if ( processId == 0 ) {
    return false;
  }

  HANDLE process = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId );
  if ( !process ) {
    return false;
  }

  wchar_t executablePath[ 32768 ] = {};
  DWORD pathLength = static_cast< DWORD >( sizeof( executablePath ) / sizeof( executablePath[ 0 ] ) );
  const BOOL pathRead = QueryFullProcessImageNameW( process, 0, executablePath, &pathLength );
  CloseHandle( process );

  if ( !pathRead || pathLength == 0 ) {
    return false;
  }

  const QString executableName = QString::fromWCharArray( executablePath, static_cast< int >( pathLength ) )
                                   .section( QChar( '\\' ), -1 )
                                   .toUpper();
  static const QStringList officeExecutables = {
    QStringLiteral( "WINWORD.EXE" ),
    QStringLiteral( "EXCEL.EXE" ),
    QStringLiteral( "POWERPNT.EXE" ),
    QStringLiteral( "OUTLOOK.EXE" ),
    QStringLiteral( "ONENOTE.EXE" )
  };

  return officeExecutables.contains( executableName );
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

static bool sutraStartupIsCjkIdeograph( const QChar & ch )
{
  const uint u = ch.unicode();
  return ( u >= 0x3400 && u <= 0x4DBF )
      || ( u >= 0x4E00 && u <= 0x9FFF )
      || ( u >= 0xF900 && u <= 0xFAFF );
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

int sutraStartupLatinTokenCount( const QString & text )
{
  int count = 0;
  for ( const SutraStartupTextToken & token : sutraStartupTokenizePhraseText( text ) ) {
    if ( token.hasLatin ) {
      ++count;
    }
  }

  return count;
}

bool sutraStartupIsUsableOfficePhraseCandidate( const QString & text )
{
  const QString cleaned = text.trimmed();
  if ( cleaned.isEmpty() ) {
    return false;
  }

  int cjkIdeographCount = 0;
  for ( const QChar & ch : cleaned ) {
    if ( sutraStartupIsCjkIdeograph( ch ) ) {
      ++cjkIdeographCount;
    }
  }

  // Office 2010 and some 32-bit Office providers frequently expose only the
  // TextUnit_Word under the pointer. Treat one Latin token or one CJK ideograph
  // as incomplete context so the compatibility path can safely capture the
  // visual line and resolve the nearest real phrase.
  if ( cjkIdeographCount == 1 ) {
    return false;
  }
  if ( sutraStartupHasLatinLetter( cleaned ) && cjkIdeographCount == 0 ) {
    return sutraStartupLatinTokenCount( cleaned ) >= 2;
  }

  return true;
}

bool sutraStartupLooksLikeReorderedLatinSelection( const QString & selectedText,
                                                    const QString & contextText )
{
  const QList< SutraStartupTextToken > selectedTokens = sutraStartupTokenizePhraseText( selectedText );
  const QList< SutraStartupTextToken > contextTokens = sutraStartupTokenizePhraseText( contextText );

  QStringList selectedNormalized;
  QStringList contextNormalized;

  for ( const SutraStartupTextToken & token : selectedTokens ) {
    if ( token.hasLatin ) {
      selectedNormalized << token.normalized;
    }
  }
  for ( const SutraStartupTextToken & token : contextTokens ) {
    if ( token.hasLatin ) {
      contextNormalized << token.normalized;
    }
  }

  if ( selectedNormalized.size() < 2 || contextNormalized.size() < 2 ) {
    return false;
  }

  const QString selectedSequence = selectedNormalized.join( QLatin1Char( ' ' ) );
  const QString contextSequence = contextNormalized.join( QLatin1Char( ' ' ) );
  if ( contextSequence.contains( selectedSequence ) ) {
    return false;
  }

  // Some Word 2016 UI Automation providers return split selection ranges in
  // reverse order. Detect only that narrow case: every selected token exists in
  // the nearby context, but not in the reported order.
  QStringList remaining = contextNormalized;
  for ( const QString & token : selectedNormalized ) {
    if ( !remaining.removeOne( token ) ) {
      return false;
    }
  }

  return true;
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

const QStringList & sutraStartupCjkGlossaryTerms()
{
  static const QStringList terms = [] {
    QStringList result;
    QSet< QString > seen;

    QFile file( QCoreApplication::applicationDirPath() + QStringLiteral( "/buddhist_terms.json" ) );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
      return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError ) {
      return result;
    }

    const QJsonArray entries = document.isArray()
                               ? document.array()
                               : document.object().value( QStringLiteral( "terms" ) ).toArray();

    const auto appendCjkLookupTerm = [ &result, &seen ]( const QString & rawTerm ) {
      const QString term = rawTerm.trimmed();
      if ( term.isEmpty() || term.size() > 80 || seen.contains( term ) ) {
        return;
      }

      int ideographCount = 0;
      for ( const QChar & ch : term ) {
        if ( sutraStartupIsCjkIdeograph( ch ) ) {
          ++ideographCount;
        }
      }

      if ( ideographCount == 0 ) {
        return;
      }

      seen.insert( term );
      result << term;
    };

    for ( const QJsonValue & value : entries ) {
      if ( !value.isObject() ) {
        continue;
      }

      const QString term = value.toObject().value( QStringLiteral( "term" ) ).toString().trimmed();
      appendCjkLookupTerm( term );

      // A number of Buddhist personal names are stored in the glossary with
      // the honorific/title suffix U+4F5B (Buddha), while the source document
      // commonly omits that final character. Add only the conservative
      // four-or-more-character base alias. This lets a source such as
      // U+91CB U+8FE6 U+725F U+5C3C resolve as one compound from the existing
      // five-character glossary entry, without changing the JSON or weakening
      // the longest-term preference when the suffix is actually present.
      if ( term.endsWith( QChar( 0x4F5B ) ) ) {
        const QString baseName = term.left( term.size() - 1 ).trimmed();
        int baseIdeographCount = 0;
        for ( const QChar & ch : baseName ) {
          if ( sutraStartupIsCjkIdeograph( ch ) ) {
            ++baseIdeographCount;
          }
        }

        if ( baseIdeographCount >= 4 ) {
          appendCjkLookupTerm( baseName );
        }
      }
    }

    std::sort( result.begin(), result.end(), []( const QString & left, const QString & right ) {
      return left.size() > right.size();
    } );

    return result;
  }();

  return terms;
}

struct SutraStartupCompactCjkText
{
  QString text;
  QList< int > originalOffsets;
};

SutraStartupCompactCjkText sutraStartupCompactCjkText( const QString & source )
{
  SutraStartupCompactCjkText result;
  result.text.reserve( source.size() );
  result.originalOffsets.reserve( source.size() );

  for ( int i = 0; i < source.size(); ++i ) {
    if ( sutraStartupIsCjkIdeograph( source.at( i ) ) ) {
      result.text += source.at( i );
      result.originalOffsets << i;
    }
  }

  return result;
}

int sutraStartupCjkIdeographCount( const QString & text )
{
  int count = 0;
  for ( const QChar & ch : text ) {
    if ( sutraStartupIsCjkIdeograph( ch ) ) {
      ++count;
    }
  }
  return count;
}

int sutraStartupCompactCjkAnchorAtOffset( const SutraStartupCompactCjkText & compact,
                                           int clickOffset )
{
  if ( compact.originalOffsets.isEmpty() ) {
    return -1;
  }

  int compactAnchor = 0;
  int nearestDistance = ( std::numeric_limits< int >::max )();
  for ( int i = 0; i < compact.originalOffsets.size(); ++i ) {
    const int distance = qAbs( compact.originalOffsets.at( i ) - clickOffset );
    if ( distance < nearestDistance ) {
      nearestDistance = distance;
      compactAnchor = i;
    }
  }
  return compactAnchor;
}

QString sutraStartupCjkFallbackWindowAtOffset( const QString & context, int clickOffset )
{
  const SutraStartupCompactCjkText compact = sutraStartupCompactCjkText( context );
  const int anchor = sutraStartupCompactCjkAnchorAtOffset( compact, clickOffset );
  if ( anchor < 0 || compact.text.isEmpty() ) {
    return {};
  }

  if ( compact.text.size() == 1 ) {
    return compact.text;
  }

  // Precise lookup must not collapse a multi-character CJK clause to one
  // ideograph. If no glossary entry matches, keep the nearest two-character
  // window so ordinary dictionaries still receive a useful word candidate.
  int start = qBound( 0, anchor - 1, compact.text.size() - 2 );
  if ( anchor == 0 ) {
    start = 0;
  }

  return compact.text.mid( start, 2 );
}

QString sutraStartupBestCjkGlossaryTermAtOffset( const QString & context, int clickOffset )
{
  const SutraStartupCompactCjkText compact = sutraStartupCompactCjkText( context );
  const int compactAnchor = sutraStartupCompactCjkAnchorAtOffset( compact, clickOffset );
  if ( compactAnchor < 0 || compact.text.isEmpty() ) {
    return {};
  }

  QString bestContainingTerm;
  int bestContainingLength = -1;
  int bestContainingCenterDistance = ( std::numeric_limits< int >::max )();

  QString bestNearbyTerm;
  int bestNearbyLength = -1;
  int bestNearbyDistance = ( std::numeric_limits< int >::max )();

  QString singleCharacterFallback;

  // This is the requested decreasing-length search in indexed form: glossary
  // entries are already sorted from longest to shortest, and every occurrence
  // around the pointer is evaluated. Multi-character terms always outrank a
  // one-character entry when the punctuation-delimited clause has 2+ CJK chars.
  for ( const QString & term : sutraStartupCjkGlossaryTerms() ) {
    const QString compactTerm = sutraStartupCompactCjkText( term ).text;
    if ( compactTerm.isEmpty() || compactTerm.size() > compact.text.size() ) {
      continue;
    }

    int occurrence = compact.text.indexOf( compactTerm );
    while ( occurrence >= 0 ) {
      const int end = occurrence + compactTerm.size() - 1;
      const bool containsAnchor = compactAnchor >= occurrence && compactAnchor <= end;
      const int edgeDistance = containsAnchor
                             ? 0
                             : qMin( qAbs( compactAnchor - occurrence ), qAbs( compactAnchor - end ) );
      const int centerTimesTwo = occurrence + end;
      const int centerDistance = qAbs( compactAnchor * 2 - centerTimesTwo );

      if ( compactTerm.size() == 1 && compact.text.size() > 1 ) {
        if ( containsAnchor && singleCharacterFallback.isEmpty() ) {
          singleCharacterFallback = term;
        }
        occurrence = compact.text.indexOf( compactTerm, occurrence + 1 );
        continue;
      }

      if ( containsAnchor ) {
        if ( compactTerm.size() > bestContainingLength
          || ( compactTerm.size() == bestContainingLength
               && centerDistance < bestContainingCenterDistance ) ) {
          bestContainingTerm = term;
          bestContainingLength = compactTerm.size();
          bestContainingCenterDistance = centerDistance;
        }
      }
      else if ( edgeDistance <= 1
                && ( edgeDistance < bestNearbyDistance
                     || ( edgeDistance == bestNearbyDistance
                          && compactTerm.size() > bestNearbyLength ) ) ) {
        bestNearbyTerm = term;
        bestNearbyLength = compactTerm.size();
        bestNearbyDistance = edgeDistance;
      }

      occurrence = compact.text.indexOf( compactTerm, occurrence + 1 );
    }
  }

  if ( !bestContainingTerm.isEmpty() ) {
    return bestContainingTerm;
  }
  if ( !bestNearbyTerm.isEmpty() ) {
    return bestNearbyTerm;
  }
  if ( compact.text.size() == 1 && !singleCharacterFallback.isEmpty() ) {
    return singleCharacterFallback;
  }

  return {};
}

QString sutraStartupBestKnownCjkTermInText( const QString & text, const QString & anchorText = QString() )
{
  const SutraStartupCompactCjkText compactText = sutraStartupCompactCjkText( text );
  const SutraStartupCompactCjkText compactAnchor = sutraStartupCompactCjkText( anchorText );

  if ( compactText.text.isEmpty() ) {
    return {};
  }

  QString best;
  for ( const QString & term : sutraStartupCjkGlossaryTerms() ) {
    const QString compactTerm = sutraStartupCompactCjkText( term ).text;
    if ( compactTerm.isEmpty() || !compactText.text.contains( compactTerm ) ) {
      continue;
    }

    if ( !compactAnchor.text.isEmpty()
      && !compactTerm.contains( compactAnchor.text )
      && !compactAnchor.text.contains( compactTerm ) ) {
      continue;
    }

    if ( best.isEmpty() || compactTerm.size() > sutraStartupCompactCjkText( best ).text.size() ) {
      best = term;
    }
  }

  return best;
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

enum class SutraStartupAnchorScript
{
  Unknown,
  Latin,
  Cjk
};

SutraStartupAnchorScript sutraStartupScriptNearOffset( const QString & text, int offset )
{
  if ( text.isEmpty() ) {
    return SutraStartupAnchorScript::Unknown;
  }

  const int boundedOffset = qBound( 0, offset, text.size() - 1 );
  for ( int distance = 0; distance <= 12; ++distance ) {
    const int candidates[] = { boundedOffset - distance, boundedOffset + distance };
    for ( const int index : candidates ) {
      if ( index < 0 || index >= text.size() ) {
        continue;
      }

      const QChar ch = text.at( index );
      if ( sutraStartupIsCjkIdeograph( ch ) ) {
        return SutraStartupAnchorScript::Cjk;
      }
      if ( ch.isLetter() && ch.unicode() < 0x024F ) {
        return SutraStartupAnchorScript::Latin;
      }
    }
  }

  return SutraStartupAnchorScript::Unknown;
}

QString sutraStartupBestGlossaryPhraseAtOffset( const QString & text, int clickOffset )
{
  const SutraStartupAnchorScript script = sutraStartupScriptNearOffset( text, clickOffset );
  const QString cjk = sutraStartupBestCjkGlossaryTermAtOffset( text, clickOffset );
  const QString vietnamese = sutraStartupBestVietnamesePhraseAtOffset( text, clickOffset );

  if ( script == SutraStartupAnchorScript::Cjk && !cjk.isEmpty() ) {
    return cjk;
  }
  if ( script == SutraStartupAnchorScript::Latin && !vietnamese.isEmpty() ) {
    return vietnamese;
  }

  if ( !cjk.isEmpty() && vietnamese.isEmpty() ) {
    return cjk;
  }
  if ( cjk.isEmpty() && !vietnamese.isEmpty() ) {
    return vietnamese;
  }

  // For punctuation or whitespace between scripts, prefer the more specific
  // candidate rather than always preferring CJK merely because it is present.
  if ( !cjk.isEmpty() && !vietnamese.isEmpty() ) {
    const int vietnameseTokenCount = sutraStartupTokenizePhraseText( vietnamese ).size();
    return vietnameseTokenCount >= 2 && vietnamese.size() > cjk.size() ? vietnamese : cjk;
  }

  return {};
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

    int bestAnchorDistance = ( std::numeric_limits< int >::max )();
    for ( int start = 0; start + normalizedAnchorTokens.size() <= lineTokens.size(); ++start ) {
      if ( !sutraStartupTokenSequenceMatches( lineTokens, start, normalizedAnchorTokens ) ) {
        continue;
      }

      const int end = start + normalizedAnchorTokens.size() - 1;
      const int occurrenceCenterTimesTwo = lineTokens.at( start ).start
                                         + lineTokens.at( end ).end;
      const int lineCenterTimesTwo = lineText.size();
      const int distance = qAbs( occurrenceCenterTimesTwo - lineCenterTimesTwo );

      if ( distance < bestAnchorDistance ) {
        bestAnchorDistance = distance;
        anchorIndex = start + normalizedAnchorTokens.size() / 2;
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

QString sutraStartupNormalizeOfficeText( const QString & text )
{
  QString result;
  result.reserve( text.size() );

  bool lastWasSpace = false;
  for ( const QChar & ch : text ) {
    const ushort code = ch.unicode();

    // Word/Office UI Automation may expose hidden document markers that make
    // an otherwise correct phrase fail exact dictionary lookup. Remove those
    // markers before calculating offsets or sending the query to the popup.
    if ( code == 0x0007   // end-of-cell marker
      || code == 0x000B   // vertical tab
      || code == 0x000C   // form feed
      || code == 0x00AD   // soft hyphen
      || code == 0x200B   // zero-width space
      || code == 0x200C   // zero-width non-joiner
      || code == 0x200D   // zero-width joiner
      || code == 0x200E   // left-to-right mark
      || code == 0x200F   // right-to-left mark
      || ( code >= 0x202A && code <= 0x202E ) // bidi embedding controls
      || ( code >= 0x2060 && code <= 0x2069 ) // invisible word/bidi controls
      || code == 0xFEFF   // BOM / zero-width no-break space
      || code == 0xFFFC ) // object replacement character
    {
      continue;
    }

    if ( code == 0x2028 || code == 0x2029 ) {
      result += QLatin1Char( '\n' );
      lastWasSpace = false;
      continue;
    }

    if ( code == 0x00A0 || code == 0x2007 || code == 0x202F || code == 0x3000
      || ch == QLatin1Char( '\t' ) ) {
      if ( !lastWasSpace ) {
        result += QLatin1Char( ' ' );
        lastWasSpace = true;
      }
      continue;
    }

    if ( ch == QLatin1Char( '\r' ) ) {
      continue;
    }

    if ( ch == QLatin1Char( '\n' ) ) {
      while ( result.endsWith( QLatin1Char( ' ' ) ) ) {
        result.chop( 1 );
      }
      if ( !result.endsWith( QLatin1Char( '\n' ) ) ) {
        result += QLatin1Char( '\n' );
      }
      lastWasSpace = false;
      continue;
    }

    if ( ch.category() == QChar::Other_Control ) {
      continue;
    }

    if ( ch.isSpace() ) {
      if ( !lastWasSpace ) {
        result += QLatin1Char( ' ' );
        lastWasSpace = true;
      }
      continue;
    }

    result += ch;
    lastWasSpace = false;
  }

  return result.normalized( QString::NormalizationForm_C ).trimmed();
}

QString sutraStartupTextFromBstr( BSTR text )
{
  if ( !text ) {
    return {};
  }

  const QString raw = QString::fromWCharArray( text, static_cast< int >( SysStringLen( text ) ) );
  SysFreeString( text );
  return sutraStartupNormalizeOfficeText( raw );
}

bool sutraStartupIsPhraseBoundaryAt( const QString & text, int index )
{
  if ( index < 0 || index >= text.size() ) {
    return true;
  }

  const QChar ch = text.at( index );
  const uint code = ch.unicode();

  if ( ch == QLatin1Char( '\r' ) || ch == QLatin1Char( '\n' ) ) {
    return true;
  }

  switch ( code ) {
    case 0x002C: // ,
    case 0x002E: // .
    case 0x003A: // :
    case 0x003B: // ;
    case 0x003F: // ?
    case 0x0021: // !
    case 0x2026: // HORIZONTAL ELLIPSIS
    case 0x2025: // TWO DOT LEADER
    case 0x2013: // EN DASH
    case 0x2014: // EM DASH
    case 0x2015: // HORIZONTAL BAR
    case 0x2212: // MINUS SIGN
    case 0x3001: // IDEOGRAPHIC COMMA
    case 0x3002: // IDEOGRAPHIC FULL STOP
    case 0x3008: // LEFT ANGLE BRACKET
    case 0x3009: // RIGHT ANGLE BRACKET
    case 0x300A: // LEFT DOUBLE ANGLE BRACKET
    case 0x300B: // RIGHT DOUBLE ANGLE BRACKET
    case 0x300C: // LEFT CORNER BRACKET
    case 0x300D: // RIGHT CORNER BRACKET
    case 0x300E: // LEFT WHITE CORNER BRACKET
    case 0x300F: // RIGHT WHITE CORNER BRACKET
    case 0x3010: // LEFT BLACK LENTICULAR BRACKET
    case 0x3011: // RIGHT BLACK LENTICULAR BRACKET
    case 0x3014: // LEFT TORTOISE SHELL BRACKET
    case 0x3015: // RIGHT TORTOISE SHELL BRACKET
    case 0x3016: // LEFT WHITE LENTICULAR BRACKET
    case 0x3017: // RIGHT WHITE LENTICULAR BRACKET
    case 0x3018: // LEFT WHITE TORTOISE SHELL BRACKET
    case 0x3019: // RIGHT WHITE TORTOISE SHELL BRACKET
    case 0x301A: // LEFT WHITE SQUARE BRACKET
    case 0x301B: // RIGHT WHITE SQUARE BRACKET
    case 0xFF01: // FULLWIDTH EXCLAMATION MARK
    case 0xFF08: // FULLWIDTH LEFT PARENTHESIS
    case 0xFF09: // FULLWIDTH RIGHT PARENTHESIS
    case 0xFF0C: // FULLWIDTH COMMA
    case 0xFF0E: // FULLWIDTH FULL STOP
    case 0xFF1A: // FULLWIDTH COLON
    case 0xFF1B: // FULLWIDTH SEMICOLON
    case 0xFF1F: // FULLWIDTH QUESTION MARK
    case 0xFF3B: // FULLWIDTH LEFT SQUARE BRACKET
    case 0xFF3D: // FULLWIDTH RIGHT SQUARE BRACKET
    case 0x0028: // (
    case 0x0029: // )
    case 0x005B: // [
    case 0x005D: // ]
    case 0x007B: // {
    case 0x007D: // }
      return true;
    default:
      break;
  }

  // Keep transliterations such as "Bát-nhã" and "A-di-đà" intact. An ASCII
  // hyphen is a phrase boundary only when it is not joining two letters or
  // numbers. Longer dashes are always handled as boundaries above.
  if ( ch == QLatin1Char( '-' ) ) {
    const bool joinsLeft = index > 0 && text.at( index - 1 ).isLetterOrNumber();
    const bool joinsRight = index + 1 < text.size() && text.at( index + 1 ).isLetterOrNumber();
    return !( joinsLeft && joinsRight );
  }

  // Apostrophes and quotation marks delimit phrases unless they are used
  // inside a word. This covers normal Latin punctuation without breaking a
  // legitimate apostrophe inside imported dictionary text.
  if ( ch == QLatin1Char( '\'' ) || ch == QLatin1Char( '"' )
    || code == 0x2018 || code == 0x2019 || code == 0x201C || code == 0x201D ) {
    const bool joinsLeft = index > 0 && text.at( index - 1 ).isLetterOrNumber();
    const bool joinsRight = index + 1 < text.size() && text.at( index + 1 ).isLetterOrNumber();
    return !( joinsLeft && joinsRight );
  }

  return false;
}

bool sutraStartupIsCjkPhraseSeparator( const QChar & ch )
{
  const QString oneChar( ch );
  return sutraStartupIsPhraseBoundaryAt( oneChar, 0 );
}

struct SutraStartupDelimitedPhrase
{
  QString text;
  int clickOffset = -1;
};

SutraStartupDelimitedPhrase sutraStartupDelimitedPhraseAtOffset( const QString & source,
                                                                  int requestedOffset )
{
  SutraStartupDelimitedPhrase result;
  if ( source.isEmpty() ) {
    return result;
  }

  int anchor = qBound( 0, requestedOffset, source.size() - 1 );

  if ( sutraStartupIsPhraseBoundaryAt( source, anchor ) || source.at( anchor ).isSpace() ) {
    int right = anchor;
    while ( right < source.size()
            && ( sutraStartupIsPhraseBoundaryAt( source, right ) || source.at( right ).isSpace() ) ) {
      ++right;
    }

    int left = anchor - 1;
    while ( left >= 0
            && ( sutraStartupIsPhraseBoundaryAt( source, left ) || source.at( left ).isSpace() ) ) {
      --left;
    }

    if ( right < source.size() ) {
      anchor = right;
    }
    else if ( left >= 0 ) {
      anchor = left;
    }
    else {
      return result;
    }
  }

  int start = anchor;
  while ( start > 0 && !sutraStartupIsPhraseBoundaryAt( source, start - 1 ) ) {
    --start;
  }

  int end = anchor + 1;
  while ( end < source.size() && !sutraStartupIsPhraseBoundaryAt( source, end ) ) {
    ++end;
  }

  while ( start < end && source.at( start ).isSpace() ) {
    ++start;
  }
  while ( end > start && source.at( end - 1 ).isSpace() ) {
    --end;
  }

  if ( start >= end ) {
    return result;
  }

  result.text = source.mid( start, end - start ).trimmed();
  result.clickOffset = qBound( 0, anchor - start, qMax( 0, result.text.size() - 1 ) );
  return result;
}

QString sutraStartupSmallCjkWindowAtOffset( const QString & text, int clickOffset )
{
  if ( text.isEmpty() ) {
    return {};
  }

  int anchor = qBound( 0, clickOffset, text.size() - 1 );
  while ( anchor < text.size() && !sutraStartupIsCjkIdeograph( text.at( anchor ) ) ) {
    ++anchor;
  }
  if ( anchor >= text.size() ) {
    anchor = qBound( 0, clickOffset - 1, text.size() - 1 );
    while ( anchor >= 0 && !sutraStartupIsCjkIdeograph( text.at( anchor ) ) ) {
      --anchor;
    }
  }

  if ( anchor < 0 || anchor >= text.size() ) {
    return {};
  }

  int clauseStart = anchor;
  while ( clauseStart > 0 && !sutraStartupIsCjkPhraseSeparator( text.at( clauseStart - 1 ) ) ) {
    --clauseStart;
  }

  int clauseEnd = anchor + 1;
  while ( clauseEnd < text.size() && !sutraStartupIsCjkPhraseSeparator( text.at( clauseEnd ) ) ) {
    ++clauseEnd;
  }

  constexpr int maxWindowChars = 16;
  if ( clauseEnd - clauseStart > maxWindowChars ) {
    clauseStart = qMax( clauseStart, anchor - maxWindowChars / 2 );
    clauseEnd = qMin( clauseEnd, clauseStart + maxWindowChars );
    if ( clauseEnd - clauseStart < maxWindowChars ) {
      clauseStart = qMax( 0, clauseEnd - maxWindowChars );
    }
  }

  return text.mid( clauseStart, clauseEnd - clauseStart ).trimmed();
}

bool sutraStartupCenteredUiAutomationContext( IUIAutomationTextRange * pointRange,
                                               QString * contextText,
                                               int * clickOffset )
{
  if ( !pointRange || !contextText || !clickOffset ) {
    return false;
  }

  IUIAutomationTextRange * contextRange = nullptr;
  if ( FAILED( pointRange->Clone( &contextRange ) ) || !contextRange ) {
    return false;
  }

  int movedStart = 0;
  int movedEnd = 0;
  contextRange->MoveEndpointByUnit( TextPatternRangeEndpoint_Start, TextUnit_Character, -120, &movedStart );
  contextRange->MoveEndpointByUnit( TextPatternRangeEndpoint_End, TextUnit_Character, 120, &movedEnd );
  Q_UNUSED( movedEnd );

  BSTR contextBstr = nullptr;
  if ( FAILED( contextRange->GetText( 260, &contextBstr ) ) ) {
    contextRange->Release();
    return false;
  }

  const QString text = sutraStartupTextFromBstr( contextBstr );
  if ( text.isEmpty() ) {
    contextRange->Release();
    return false;
  }

  // MoveEndpointByUnit reports the number of characters moved even on some
  // Office proxies where MoveEndpointByRange later fails. Use it as a safe
  // cross-bitness fallback for the pointer offset.
  int offset = qAbs( movedStart );
  IUIAutomationTextRange * leftRange = nullptr;
  if ( SUCCEEDED( contextRange->Clone( &leftRange ) ) && leftRange ) {
    if ( SUCCEEDED( leftRange->MoveEndpointByRange( TextPatternRangeEndpoint_End,
                                                    pointRange,
                                                    TextPatternRangeEndpoint_Start ) ) ) {
      BSTR leftBstr = nullptr;
      if ( SUCCEEDED( leftRange->GetText( 260, &leftBstr ) ) ) {
        offset = sutraStartupTextFromBstr( leftBstr ).size();
      }
    }
    leftRange->Release();
  }

  contextRange->Release();

  if ( offset < 0 ) {
    return false;
  }

  *contextText = text;
  *clickOffset = qBound( 0, offset, text.size() );
  return true;
}

QString sutraStartupLookupFromDelimitedContext( const QString & text,
                                                  int clickOffset,
                                                  SutraMouseLookupCaptureMode captureMode )
{
  const SutraStartupDelimitedPhrase delimited = sutraStartupDelimitedPhraseAtOffset( text, clickOffset );
  if ( delimited.text.isEmpty() ) {
    return {};
  }

  if ( captureMode == SutraMouseLookupCaptureMode::EntirePhrase ) {
    // Keep the whole punctuation-delimited phrase for online lookup and for the
    // multi-term renderer in the glossary tab. A generous cap prevents a
    // malformed accessibility provider from returning an entire document.
    return delimited.text.left( 360 ).trimmed();
  }

  const QString phrase = sutraStartupBestGlossaryPhraseAtOffset( delimited.text,
                                                                  delimited.clickOffset );
  if ( !phrase.isEmpty() && phrase.size() <= 160 ) {
    return phrase;
  }

  if ( sutraStartupCjkIdeographCount( delimited.text ) > 0 ) {
    return sutraStartupCjkFallbackWindowAtOffset( delimited.text,
                                                   delimited.clickOffset );
  }

  return {};
}

QString sutraStartupTextFromUiAutomationRange( IUIAutomationTextRange * range,
                                                SutraMouseLookupCaptureMode captureMode,
                                                bool officeCompatibilityMode )
{
  if ( !range ) {
    return {};
  }

  // Word versions expose very different TextUnit boundaries. Build both a
  // centered character context and an enclosing visual line. For Latin text in
  // Microsoft Office, prefer TextUnit_Line because endpoint expansion around a
  // point is known to split or reverse words on some Word 2016 providers.
  QString centeredText;
  int centeredOffset = -1;
  const bool hasCenteredContext = sutraStartupCenteredUiAutomationContext( range,
                                                                           &centeredText,
                                                                           &centeredOffset );

  QString centeredCandidate;
  if ( hasCenteredContext ) {
    centeredCandidate = sutraStartupLookupFromDelimitedContext( centeredText,
                                                                centeredOffset,
                                                                captureMode );

    const bool centeredIsPureLatin = sutraStartupHasLatinLetter( centeredCandidate )
                                    && sutraStartupCjkIdeographCount( centeredCandidate ) == 0;
    if ( !centeredCandidate.isEmpty()
      && ( !officeCompatibilityMode
        || ( !centeredIsPureLatin
          && sutraStartupIsUsableOfficePhraseCandidate( centeredCandidate ) ) ) ) {
      return centeredCandidate;
    }
  }

  IUIAutomationTextRange * lineRange = nullptr;
  if ( FAILED( range->Clone( &lineRange ) ) || !lineRange ) {
    return !officeCompatibilityMode || sutraStartupIsUsableOfficePhraseCandidate( centeredCandidate )
             ? centeredCandidate
             : QString();
  }

  lineRange->ExpandToEnclosingUnit( TextUnit_Line );

  BSTR lineBstr = nullptr;
  if ( FAILED( lineRange->GetText( 700, &lineBstr ) ) ) {
    lineRange->Release();
    return !officeCompatibilityMode || sutraStartupIsUsableOfficePhraseCandidate( centeredCandidate )
             ? centeredCandidate
             : QString();
  }

  const QString lineText = sutraStartupTextFromBstr( lineBstr );
  int clickOffset = -1;

  IUIAutomationTextRange * prefixRange = nullptr;
  if ( SUCCEEDED( lineRange->Clone( &prefixRange ) ) && prefixRange ) {
    if ( SUCCEEDED( prefixRange->MoveEndpointByRange( TextPatternRangeEndpoint_End,
                                                      range,
                                                      TextPatternRangeEndpoint_Start ) ) ) {
      BSTR prefixBstr = nullptr;
      if ( SUCCEEDED( prefixRange->GetText( 700, &prefixBstr ) ) ) {
        clickOffset = sutraStartupTextFromBstr( prefixBstr ).size();
      }
    }
    prefixRange->Release();
  }

  lineRange->Release();

  if ( clickOffset >= 0 && !lineText.isEmpty() ) {
    const QString lineCandidate = sutraStartupLookupFromDelimitedContext( lineText,
                                                                          clickOffset,
                                                                          captureMode );
    if ( !lineCandidate.isEmpty()
      && ( !officeCompatibilityMode
        || sutraStartupIsUsableOfficePhraseCandidate( lineCandidate ) ) ) {
      return lineCandidate;
    }
  }

  // If the line provider did not expose a useful pointer offset, a centered
  // multi-token candidate remains preferable to a one-word TextUnit fallback.
  if ( !centeredCandidate.isEmpty()
    && ( !officeCompatibilityMode
      || sutraStartupIsUsableOfficePhraseCandidate( centeredCandidate ) ) ) {
    return centeredCandidate;
  }

  // Some controls expose only TextUnit_Word. Preserve that behavior outside
  // Microsoft Office. In Office, reject a one-token Latin result so the safe
  // compatibility line-capture path can obtain enough context.
  IUIAutomationTextRange * wordRange = nullptr;
  if ( SUCCEEDED( range->Clone( &wordRange ) ) && wordRange ) {
    wordRange->ExpandToEnclosingUnit( TextUnit_Word );

    BSTR wordBstr = nullptr;
    if ( SUCCEEDED( wordRange->GetText( 220, &wordBstr ) ) ) {
      const QString wordText = sutraStartupTextFromBstr( wordBstr ).trimmed();
      wordRange->Release();

      if ( !wordText.isEmpty() ) {
        if ( captureMode == SutraMouseLookupCaptureMode::EntirePhrase ) {
          if ( !officeCompatibilityMode
            || sutraStartupIsUsableOfficePhraseCandidate( wordText ) ) {
            return wordText.left( 360 );
          }
        }
        else if ( sutraStartupHasLatinLetter( wordText ) ) {
          const QString phrase = sutraStartupBestVietnamesePhraseFromLine( wordText, wordText );
          if ( !phrase.isEmpty()
            && ( !officeCompatibilityMode
              || sutraStartupIsUsableOfficePhraseCandidate( phrase ) ) ) {
            return phrase;
          }
        }

        const int wordCjkCount = sutraStartupCjkIdeographCount( wordText );
        const QString knownTerm = sutraStartupBestKnownCjkTermInText( wordText );
        if ( !knownTerm.isEmpty()
          && sutraStartupCjkIdeographCount( knownTerm ) >= 2 ) {
          return knownTerm;
        }

        if ( wordCjkCount >= 2 && wordText.size() <= 80 ) {
          return wordText;
        }
        if ( wordCjkCount == 0 && wordText.size() <= 80 && !officeCompatibilityMode ) {
          return wordText;
        }
      }
    }
    else {
      wordRange->Release();
    }
  }

  if ( captureMode == SutraMouseLookupCaptureMode::Automatic && clickOffset >= 0 ) {
    const SutraStartupDelimitedPhrase delimited = sutraStartupDelimitedPhraseAtOffset( lineText,
                                                                                        clickOffset );
    const QString cjkWindow = sutraStartupCjkFallbackWindowAtOffset(
      !delimited.text.isEmpty() ? delimited.text : lineText,
      !delimited.text.isEmpty() ? delimited.clickOffset : clickOffset );
    if ( !cjkWindow.isEmpty() ) {
      return cjkWindow;
    }
  }

  if ( captureMode == SutraMouseLookupCaptureMode::EntirePhrase && !lineText.isEmpty() ) {
    const SutraStartupDelimitedPhrase delimited = sutraStartupDelimitedPhraseAtOffset(
      lineText,
      clickOffset >= 0 ? clickOffset : lineText.size() / 2 );
    const QString candidate = delimited.text.left( 360 ).trimmed();
    if ( !officeCompatibilityMode || sutraStartupIsUsableOfficePhraseCandidate( candidate ) ) {
      return candidate;
    }
  }

  if ( sutraStartupHasLatinLetter( lineText ) ) {
    const QString candidate = sutraStartupBestVietnamesePhraseFromLine( lineText, QString() ).left( 160 );
    if ( !officeCompatibilityMode || sutraStartupIsUsableOfficePhraseCandidate( candidate ) ) {
      return candidate;
    }
  }

  return {};
}


QList< POINT > sutraStartupUiAutomationProbePoints( const QPoint & globalPos )
{
  // UI Automation hit testing in Word is not fully stable at larger font sizes:
  // RangeFromPoint can land on the glyph edge or an insertion boundary and
  // return no text even though the pointer is visibly over the character.
  // Probe a compact, distance-ordered neighborhood. The radius remains well
  // inside a normal 24-28 pt glyph/line, so it corrects hit-test rounding
  // without drifting into unrelated paragraphs.
  static const QPoint offsets[] = {
    QPoint( 0, 0 ),
    QPoint( 0, -2 ), QPoint( 0, 2 ), QPoint( -2, 0 ), QPoint( 2, 0 ),
    QPoint( 0, -5 ), QPoint( 0, 5 ), QPoint( -5, 0 ), QPoint( 5, 0 ),
    QPoint( -4, -4 ), QPoint( 4, -4 ), QPoint( -4, 4 ), QPoint( 4, 4 ),
    QPoint( 0, -7 ), QPoint( 0, 7 ),
    QPoint( -9, 0 ), QPoint( 9, 0 ), QPoint( -12, 0 ), QPoint( 12, 0 )
  };

  QList< POINT > points;
  points.reserve( int( sizeof( offsets ) / sizeof( offsets[ 0 ] ) ) );

  for ( const QPoint & offset : offsets ) {
    POINT point;
    point.x = globalPos.x() + offset.x();
    point.y = globalPos.y() + offset.y();
    points << point;
  }

  return points;
}

bool sutraStartupIsKnownCjkLookupCandidate( const QString & candidate )
{
  static const QSet< QString > knownTerms = [] {
    QSet< QString > result;
    for ( const QString & term : sutraStartupCjkGlossaryTerms() ) {
      result.insert( term );
    }
    return result;
  }();

  return knownTerms.contains( candidate.trimmed() );
}

int sutraStartupUiAutomationCandidateScore( const QString & candidate,
                                            SutraMouseLookupCaptureMode captureMode,
                                            const POINT & probe,
                                            const QPoint & origin )
{
  const QString cleaned = candidate.trimmed();
  if ( cleaned.isEmpty() ) {
    return ( std::numeric_limits< int >::min )();
  }

  const int distance = qAbs( int( probe.x ) - origin.x() )
                     + qAbs( int( probe.y ) - origin.y() );

  if ( captureMode == SutraMouseLookupCaptureMode::EntirePhrase ) {
    // For full-clause lookup, pointer proximity is more important than length:
    // a neighboring punctuation section must never outrank the exact clause.
    return 20000 - distance * 100 + qMin( cleaned.size(), 360 );
  }

  const int cjkCount = sutraStartupCjkIdeographCount( cleaned );
  if ( cjkCount > 0 ) {
    int score = 10000 + cjkCount * 200 - distance * 40;

    // A real glossary term (including the conservative title-free aliases)
    // should outrank a generic two-character fallback from a glyph edge.
    if ( sutraStartupIsKnownCjkLookupCandidate( cleaned ) ) {
      score += 50000;
    }

    if ( cjkCount == 1 ) {
      score -= 6000;
    }

    return score;
  }

  const int latinTokens = sutraStartupLatinTokenCount( cleaned );
  return 5000 + latinTokens * 200 + qMin( cleaned.size(), 160 ) - distance * 40;
}

QString sutraStartupUiAutomationTextAtPoint( const QPoint & globalPos,
                                                   SutraMouseLookupCaptureMode captureMode )
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

  const bool officeCompatibilityMode = sutraStartupIsMicrosoftOfficeWindowAtPoint( globalPos );
  QList< POINT > probePoints;
  if ( officeCompatibilityMode ) {
    probePoints = sutraStartupUiAutomationProbePoints( globalPos );
  }
  else {
    probePoints << point;
  }

  QString result;
  IUIAutomationElement * currentElement = element;

  for ( int depth = 0; currentElement && depth < 8 && result.trimmed().isEmpty(); ++depth ) {
    IUIAutomationTextPattern * textPattern = nullptr;

    hr = currentElement->GetCurrentPatternAs( UIA_TextPatternId,
                                              IID_PPV_ARGS( &textPattern ) );

    if ( SUCCEEDED( hr ) && textPattern ) {
      QString bestCandidate;
      int bestScore = ( std::numeric_limits< int >::min )();

      for ( const POINT & probePoint : probePoints ) {
        IUIAutomationTextRange * textRange = nullptr;
        hr = textPattern->RangeFromPoint( probePoint, &textRange );

        if ( FAILED( hr ) || !textRange ) {
          continue;
        }

        const QString candidate = sutraStartupTextFromUiAutomationRange(
          textRange,
          captureMode,
          officeCompatibilityMode );
        textRange->Release();

        if ( candidate.trimmed().isEmpty() ) {
          continue;
        }

        if ( captureMode != SutraMouseLookupCaptureMode::Automatic ) {
          bestCandidate = candidate;
          break;
        }

        const int score = sutraStartupUiAutomationCandidateScore( candidate,
                                                                   captureMode,
                                                                   probePoint,
                                                                   globalPos );
        if ( score > bestScore ) {
          bestScore = score;
          bestCandidate = candidate;
        }

        // The exact pointer already produced a long, known CJK compound.
        // No neighboring probe can improve its semantic precision.
        if ( probePoint.x == point.x && probePoint.y == point.y
          && sutraStartupCjkIdeographCount( candidate ) >= 4
          && sutraStartupIsKnownCjkLookupCandidate( candidate ) ) {
          break;
        }
      }

      result = bestCandidate.trimmed();
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

QString sutraStartupSelectedTextFromPattern( IUIAutomationTextPattern * textPattern,
                                              const POINT * point = nullptr )
{
  if ( !textPattern ) {
    return {};
  }

  IUIAutomationTextRange * pointRange = nullptr;
  if ( point ) {
    textPattern->RangeFromPoint( *point, &pointRange );
  }

  IUIAutomationTextRangeArray * ranges = nullptr;
  if ( FAILED( textPattern->GetSelection( &ranges ) ) || !ranges ) {
    if ( pointRange ) {
      pointRange->Release();
    }
    return {};
  }

  int length = 0;
  ranges->get_Length( &length );

  QList< IUIAutomationTextRange * > selectedRanges;
  bool pointInsideSelection = pointRange == nullptr;

  for ( int i = 0; i < length; ++i ) {
    IUIAutomationTextRange * range = nullptr;
    if ( FAILED( ranges->GetElement( i, &range ) ) || !range ) {
      continue;
    }

    if ( pointRange ) {
      int startsBeforeOrAtPoint = 1;
      int endsAfterOrAtPoint = -1;
      const HRESULT startHr = range->CompareEndpoints( TextPatternRangeEndpoint_Start,
                                                       pointRange,
                                                       TextPatternRangeEndpoint_Start,
                                                       &startsBeforeOrAtPoint );
      const HRESULT endHr = range->CompareEndpoints( TextPatternRangeEndpoint_End,
                                                     pointRange,
                                                     TextPatternRangeEndpoint_Start,
                                                     &endsAfterOrAtPoint );
      if ( SUCCEEDED( startHr ) && SUCCEEDED( endHr )
        && startsBeforeOrAtPoint <= 0 && endsAfterOrAtPoint >= 0 ) {
        pointInsideSelection = true;
      }
    }

    selectedRanges << range;
  }

  ranges->Release();
  if ( pointRange ) {
    pointRange->Release();
  }

  if ( !pointInsideSelection ) {
    for ( IUIAutomationTextRange * range : selectedRanges ) {
      range->Release();
    }
    return {};
  }

  // GetSelection() does not guarantee document order. Word 2016 can expose a
  // Vietnamese selection as several ranges in reverse order. Sort by each
  // range's start endpoint before joining the text.
  std::stable_sort( selectedRanges.begin(),
                    selectedRanges.end(),
                    []( IUIAutomationTextRange * left, IUIAutomationTextRange * right ) {
    int comparison = 0;
    return SUCCEEDED( left->CompareEndpoints( TextPatternRangeEndpoint_Start,
                                              right,
                                              TextPatternRangeEndpoint_Start,
                                              &comparison ) )
        && comparison < 0;
  } );

  QStringList selectedParts;
  for ( IUIAutomationTextRange * range : selectedRanges ) {
    BSTR text = nullptr;
    if ( SUCCEEDED( range->GetText( 600, &text ) ) ) {
      const QString part = sutraStartupTextFromBstr( text ).trimmed();
      if ( !part.isEmpty() ) {
        selectedParts << part;
      }
    }
    range->Release();
  }

  return selectedParts.join( QLatin1Char( ' ' ) ).trimmed();
}

QString sutraStartupUiAutomationSelectedTextAtPoint( const QPoint & globalPos )
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

  for ( int depth = 0; currentElement && depth < 8 && result.isEmpty(); ++depth ) {
    IUIAutomationTextPattern * textPattern = nullptr;
    hr = currentElement->GetCurrentPatternAs( UIA_TextPatternId, IID_PPV_ARGS( &textPattern ) );

    if ( SUCCEEDED( hr ) && textPattern ) {
      result = sutraStartupSelectedTextFromPattern( textPattern, &point );
      textPattern->Release();
    }

    if ( !result.isEmpty() || !walker ) {
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
  QString result = sutraStartupNormalizeOfficeText( text );
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

int sutraStartupCjkAnchorOffsetFromText( const QString & context,
                                           const QString & anchorText )
{
  const SutraStartupCompactCjkText compactContext = sutraStartupCompactCjkText( context );
  const QString compactAnchor = sutraStartupCompactCjkText( anchorText ).text;
  if ( compactContext.text.isEmpty() ) {
    return -1;
  }

  if ( compactAnchor.isEmpty() ) {
    const int center = compactContext.text.size() / 2;
    return compactContext.originalOffsets.at( qBound( 0, center, compactContext.originalOffsets.size() - 1 ) );
  }

  int bestOccurrence = -1;
  int bestCenterDistance = ( std::numeric_limits< int >::max )();
  int occurrence = compactContext.text.indexOf( compactAnchor );
  while ( occurrence >= 0 ) {
    const int occurrenceCenterTimesTwo = occurrence * 2 + compactAnchor.size() - 1;
    const int contextCenterTimesTwo = compactContext.text.size() - 1;
    const int distance = qAbs( occurrenceCenterTimesTwo - contextCenterTimesTwo );
    if ( distance < bestCenterDistance ) {
      bestCenterDistance = distance;
      bestOccurrence = occurrence;
    }
    occurrence = compactContext.text.indexOf( compactAnchor, occurrence + 1 );
  }

  if ( bestOccurrence < 0 ) {
    const int center = compactContext.text.size() / 2;
    return compactContext.originalOffsets.at( qBound( 0, center, compactContext.originalOffsets.size() - 1 ) );
  }

  const int anchorIndex = bestOccurrence + compactAnchor.size() / 2;
  return compactContext.originalOffsets.at( qBound( 0, anchorIndex, compactContext.originalOffsets.size() - 1 ) );
}

QString sutraStartupExplicitSelectionLookupText( const QString & selectedText )
{
  const QString selected = sutraStartupCleanLookupText( selectedText ).trimmed();
  if ( selected.isEmpty() || selected.size() > 160 ) {
    return {};
  }

  if ( sutraStartupCjkIdeographCount( selected ) >= 2 ) {
    return selected;
  }

  const QList< SutraStartupTextToken > tokens = sutraStartupTokenizePhraseText( selected );
  int latinTokenCount = 0;
  for ( const SutraStartupTextToken & token : tokens ) {
    if ( token.hasLatin ) {
      ++latinTokenCount;
    }
  }
  return latinTokenCount >= 2 ? selected : QString();
}

QString sutraStartupBestAutomaticLookupText( const QString & capturedText, const QString & contextText )
{
  const QString captured = sutraStartupCleanLookupText( capturedText );
  const QString context = sutraStartupCleanLookupText( contextText );
  const QString combined = !context.isEmpty() ? context : captured;
  if ( combined.isEmpty() ) {
    return {};
  }

  // A real multi-character selection under the pointer is an explicit user
  // instruction. Preserve it exactly instead of shrinking it to one CJK char.
  const QString explicitSelection = sutraStartupExplicitSelectionLookupText( captured );
  if ( !explicitSelection.isEmpty() ) {
    return explicitSelection;
  }

  const int capturedCjkCount = sutraStartupCjkIdeographCount( captured );
  const int combinedCjkCount = sutraStartupCjkIdeographCount( combined );
  const bool capturedHasLatin = sutraStartupHasLatinLetter( captured );
  const bool combinedHasLatin = sutraStartupHasLatinLetter( combined );

  if ( capturedHasLatin && capturedCjkCount == 0 ) {
    const QString phrase = sutraStartupBestVietnamesePhraseFromLine( combined, captured );
    if ( !phrase.isEmpty() ) {
      return phrase;
    }
  }

  if ( combinedCjkCount > 0 ) {
    const int anchorOffset = sutraStartupCjkAnchorOffsetFromText( combined, captured );
    if ( anchorOffset >= 0 ) {
      const QString known = sutraStartupBestCjkGlossaryTermAtOffset( combined, anchorOffset );
      if ( !known.isEmpty() ) {
        return known;
      }

      const QString fallback = sutraStartupCjkFallbackWindowAtOffset( combined, anchorOffset );
      if ( !fallback.isEmpty() ) {
        return fallback;
      }
    }
  }

  if ( combinedHasLatin ) {
    const QString phrase = sutraStartupBestVietnamesePhraseFromLine( combined, captured );
    if ( !phrase.isEmpty() ) {
      return phrase;
    }
  }

  if ( !context.isEmpty() && !sutraStartupShouldUseLineFallbackForPhrase( context ) ) {
    return context.left( 160 ).trimmed();
  }

  // Never fall back to an unbounded multi-character CJK clause after precise
  // matching failed. Returning no result lets the Office compatibility path
  // obtain a safer selection instead of opening a wrong one-character query.
  if ( combinedCjkCount > 1 ) {
    return {};
  }

  return combined.left( 160 ).trimmed();
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

  static void populateCaptureModeCombo( QComboBox * combo )
  {
    combo->addItem( QObject::tr( "Automatic - detect precise phrase under pointer" ),
                    static_cast< int >( SutraMouseLookupCaptureMode::Automatic ) );
    combo->addItem( QObject::tr( "Automatic - detect entire phrase under pointer" ),
                    static_cast< int >( SutraMouseLookupCaptureMode::EntirePhrase ) );
    combo->addItem( QObject::tr( "Manual - use selected text only" ),
                    static_cast< int >( SutraMouseLookupCaptureMode::SelectedText ) );
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

    QHBoxLayout * captureLayout = new QHBoxLayout;
    QLabel * captureLabel = new QLabel( QObject::tr( "Text capture:" ), group );
    QComboBox * captureCombo = new QComboBox( group );
    captureCombo->setObjectName( QStringLiteral( "sutraMouseLookupCaptureMode" ) );
    populateCaptureModeCombo( captureCombo );
    captureCombo->setToolTip( QObject::tr(
      "Precise automatic mode limits text to the punctuation-delimited phrase and then selects the best glossary term. "
      "Entire phrase mode keeps the full punctuation-delimited phrase for online lookup and the glossary tab. "
      "Manual mode keeps your existing selection and translates only the highlighted text." ) );
    captureLayout->addWidget( captureLabel );
    captureLayout->addWidget( captureCombo, 1 );
    groupLayout->addLayout( captureLayout );

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

    int captureIndex = captureCombo->findData( static_cast< int >( current.captureMode ) );
    captureCombo->setCurrentIndex( captureIndex >= 0 ? captureIndex : 0 );

    auto updateControls = [ enabled, modifierCombo, buttonCombo, captureCombo, currentLabel ] {
      modifierCombo->setEnabled( enabled->isChecked() );
      buttonCombo->setEnabled( enabled->isChecked() );
      captureCombo->setEnabled( enabled->isChecked() );

      SutraMouseLookupSettings value;
      value.enabled = enabled->isChecked();
      value.modifiers = modifierCombo->currentData().toInt();
      value.button = static_cast< SutraMouseLookupButton >( buttonCombo->currentData().toInt() );
      value.captureMode = static_cast< SutraMouseLookupCaptureMode >( captureCombo->currentData().toInt() );
      currentLabel->setText( QObject::tr( "Current mouse lookup: %1" ).arg( sutraMouseLookupSettingsLabel( value ) ) );
    };

    QObject::connect( enabled, &QCheckBox::toggled, dialog, [ updateControls ] { updateControls(); } );
    QObject::connect( modifierCombo, &QComboBox::currentIndexChanged, dialog, [ updateControls ] { updateControls(); } );
    QObject::connect( buttonCombo, &QComboBox::currentIndexChanged, dialog, [ updateControls ] { updateControls(); } );
    QObject::connect( captureCombo, &QComboBox::currentIndexChanged, dialog, [ updateControls ] { updateControls(); } );
    updateControls();

    const int insertionIndex = qMax( 0, tabLayout->count() - 1 );
    tabLayout->insertWidget( insertionIndex, group );

    if ( QDialogButtonBox * buttons = dialog->findChild< QDialogButtonBox * >( QStringLiteral( "buttonBox" ) ) ) {
      QObject::connect( buttons, &QDialogButtonBox::accepted, dialog,
                        [ enabled, modifierCombo, buttonCombo, captureCombo ] {
        SutraMouseLookupSettings value;
        value.enabled = enabled->isChecked();
        value.modifiers = modifierCombo->currentData().toInt();
        value.button = static_cast< SutraMouseLookupButton >( buttonCombo->currentData().toInt() );
        value.captureMode = static_cast< SutraMouseLookupCaptureMode >( captureCombo->currentData().toInt() );
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

  static bool isConfiguredButtonUpEvent( SutraMouseLookupButton button, WPARAM wParam )
  {
    switch ( button ) {
      case SutraMouseLookupButton::Left:
        return wParam == WM_LBUTTONUP;
      case SutraMouseLookupButton::Middle:
        return wParam == WM_MBUTTONUP;
      case SutraMouseLookupButton::Right:
      default:
        return wParam == WM_RBUTTONUP;
    }
  }

  static LRESULT CALLBACK mouseProc( int code, WPARAM wParam, LPARAM lParam )
  {
    if ( code == HC_ACTION && instance ) {
      // Once the configured DOWN event is consumed, also consume its matching
      // UP event even if the user releases Ctrl/Alt/Shift first. Otherwise
      // Office can receive half of a right click and open a context menu.
      if ( instance->suppressButtonRelease
        && isConfiguredButtonUpEvent( instance->suppressedButton, wParam ) ) {
        instance->suppressButtonRelease = false;
        return 1;
      }

      const SutraMouseLookupSettings settings = loadSutraMouseLookupSettings();

      if ( settings.enabled
        && modifiersMatch( settings.modifiers )
        && isConfiguredButtonEvent( settings.button, wParam ) ) {
        if ( isConfiguredButtonDownEvent( settings.button, wParam ) ) {
          instance->suppressButtonRelease = true;
          instance->suppressedButton = settings.button;

          if ( !instance->lookupInProgress ) {
            const MSLLHOOKSTRUCT * mouseInfo = reinterpret_cast< const MSLLHOOKSTRUCT * >( lParam );
            const QPoint globalPos( mouseInfo->pt.x, mouseInfo->pt.y );

            QTimer::singleShot( 0, instance, [ globalPos ] {
              if ( instance ) {
                instance->lookupAt( globalPos );
              }
            } );
          }
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

  void restoreClipboardAndFinish( const QString & previousClipboardText )
  {
    if ( QClipboard * clipboard = QApplication::clipboard() ) {
      clipboard->setText( previousClipboardText, QClipboard::Clipboard );
    }
    lookupInProgress = false;
  }

  void waitForClipboardText( const std::function< void( const QString & ) > & callback,
                             DWORD baselineSequence,
                             int attempt = 0 )
  {
    QClipboard * clipboard = QApplication::clipboard();
    if ( !clipboard ) {
      callback( QString() );
      return;
    }

    const DWORD currentSequence = GetClipboardSequenceNumber();
    const QString text = sutraStartupNormalizeOfficeText( clipboard->text( QClipboard::Clipboard ) );
    const bool clipboardChanged = currentSequence != baselineSequence;

    // Office 365 64-bit can publish Unicode text noticeably later than the
    // selection itself. Wait for an actual clipboard sequence change so an old
    // clipboard value is never mistaken for the selected phrase.
    if ( ( clipboardChanged && !text.trimmed().isEmpty() ) || attempt >= 30 ) {
      // The clipboard was cleared immediately before Ctrl+C, so any non-empty
      // value at timeout is still safe to use even if a provider failed to
      // advance the Windows sequence counter.
      callback( text );
      return;
    }

    QTimer::singleShot( 80, this, [ this, callback, baselineSequence, attempt ] {
      waitForClipboardText( callback, baselineSequence, attempt + 1 );
    } );
  }

  void copyCurrentSelection( const std::function< void( const QString & ) > & callback )
  {
    QClipboard * clipboard = QApplication::clipboard();
    if ( !clipboard ) {
      callback( QString() );
      return;
    }

    clipboard->clear( QClipboard::Clipboard );
    const DWORD baselineSequence = GetClipboardSequenceNumber();
    sendSutraStartupCtrlC();
    waitForClipboardText( callback, baselineSequence );
  }

  void waitForPhysicalModifierRelease( const std::function< void() > & callback,
                                       int attempt = 0 )
  {
    if ( !sutraStartupAnyPhysicalModifierDown() ) {
      callback();
      return;
    }

    // Safety first: if a modifier remains physically held for several seconds,
    // cancel the clipboard fallback instead of risking a plain "c" keystroke in
    // the source document. UI Automation selection remains the primary path.
    if ( attempt >= 100 ) {
      lookupInProgress = false;
      return;
    }

    QTimer::singleShot( 50, this, [ this, callback, attempt ] {
      waitForPhysicalModifierRelease( callback, attempt + 1 );
    } );
  }

  void lookupSelectedText( const QPoint & globalPos, const QString & previousClipboardText )
  {
    // The configured mouse click is suppressed by the low-level hook, so the
    // user's highlighted selection remains intact. UI Automation is completely
    // read-only and avoids injecting Ctrl+C whenever Office exposes selection.
    const QString selectedByUiAutomation = sutraStartupUiAutomationSelectedTextAtPoint( globalPos );
    if ( !selectedByUiAutomation.trimmed().isEmpty() ) {
      openLookup( selectedByUiAutomation );
      restoreClipboardAndFinish( previousClipboardText );
      return;
    }

    // Never synthesize Ctrl key-up while the user is physically holding Ctrl.
    // On some Office 365 32-bit installations that desynchronizes keyboard
    // state and the subsequent Ctrl+C arrives as a plain "c", replacing the
    // selected text. Wait for all physical modifiers to be released first.
    waitForPhysicalModifierRelease( [ this, previousClipboardText ] {
      copyCurrentSelection( [ this, previousClipboardText ]( const QString & selectedText ) {
        if ( !selectedText.trimmed().isEmpty() ) {
          openLookup( selectedText );
        }
        restoreClipboardAndFinish( previousClipboardText );
      } );
    } );
  }

  QString automaticLookupTextFromCapture( const QString & capturedText,
                                          const QString & contextText,
                                          SutraMouseLookupCaptureMode captureMode ) const
  {
    const QString explicitSelection = sutraStartupExplicitSelectionLookupText( capturedText );
    if ( !explicitSelection.isEmpty()
      && !sutraStartupLooksLikeReorderedLatinSelection( explicitSelection, contextText ) ) {
      return explicitSelection;
    }

    if ( captureMode == SutraMouseLookupCaptureMode::EntirePhrase ) {
      const QString refreshedContext = sutraStartupCleanLookupText( contextText ).left( 360 ).trimmed();
      if ( !refreshedContext.isEmpty() ) {
        return refreshedContext;
      }

      return sutraStartupCleanLookupText( capturedText ).left( 360 ).trimmed();
    }

    return sutraStartupBestAutomaticLookupText( capturedText, contextText );
  }

  bool legacyOfficeCaptureNeedsLineContext( const QString & capturedText,
                                            const QString & contextText,
                                            SutraMouseLookupCaptureMode captureMode ) const
  {
    const QString candidate = automaticLookupTextFromCapture( capturedText,
                                                               contextText,
                                                               captureMode );
    if ( candidate.trimmed().isEmpty() ) {
      return true;
    }

    if ( sutraStartupHasLatinLetter( candidate )
      && sutraStartupCjkIdeographCount( candidate ) == 0
      && sutraStartupLatinTokenCount( candidate ) < 2 ) {
      return true;
    }

    // Keep the successful CJK path unchanged, but let legacy Office expand a
    // lone ideograph when no multi-character context was available.
    return sutraStartupCjkIdeographCount( candidate ) == 1
        && sutraStartupCjkIdeographCount( contextText ) <= 1;
  }

  void captureLegacyOfficeLineContext( const QPoint & globalPos,
                                       const QString & anchorText,
                                       SutraMouseLookupCaptureMode captureMode,
                                       const QString & previousClipboardText )
  {
    // The physical modifiers were released before entering the legacy path.
    // Use staged key events so older Word versions have time to place the caret,
    // move to the visual-line start, and extend the selection to its end.
    sendSutraStartupLeftClickAt( globalPos );

    QTimer::singleShot( 70, this, [ this, globalPos, anchorText, captureMode, previousClipboardText ] {
      sendSutraStartupVirtualKey( VK_HOME, true );
      sendSutraStartupVirtualKey( VK_HOME, false );

      QTimer::singleShot( 70, this, [ this, globalPos, anchorText, captureMode, previousClipboardText ] {
        sendSutraStartupVirtualKey( VK_SHIFT, true );
        sendSutraStartupVirtualKey( VK_END, true );
        sendSutraStartupVirtualKey( VK_END, false );
        sendSutraStartupVirtualKey( VK_SHIFT, false );

        QTimer::singleShot( 140, this, [ this, anchorText, captureMode, previousClipboardText ] {
          copyCurrentSelection( [ this, anchorText, captureMode, previousClipboardText ]( const QString & lineText ) {
            QString lookupText = automaticLookupTextFromCapture( anchorText,
                                                                 lineText,
                                                                 captureMode );

            if ( lookupText.trimmed().isEmpty() ) {
              lookupText = sutraStartupCleanLookupText( anchorText ).left( 160 ).trimmed();
            }

            if ( !lookupText.trimmed().isEmpty() ) {
              openLookup( lookupText );
            }

            restoreClipboardAndFinish( previousClipboardText );
          } );
        } );
      } );
    } );
  }

  void finishLegacyAutomaticCapture( const QPoint & globalPos,
                                     const QString & capturedText,
                                     const QString & contextText,
                                     SutraMouseLookupCaptureMode captureMode,
                                     const QString & previousClipboardText )
  {
    if ( legacyOfficeCaptureNeedsLineContext( capturedText, contextText, captureMode ) ) {
      captureLegacyOfficeLineContext( globalPos,
                                      capturedText,
                                      captureMode,
                                      previousClipboardText );
      return;
    }

    const QString lookupText = automaticLookupTextFromCapture( capturedText,
                                                               contextText,
                                                               captureMode );
    if ( !lookupText.trimmed().isEmpty() ) {
      openLookup( lookupText );
    }

    restoreClipboardAndFinish( previousClipboardText );
  }

  void lookupAutomaticTextWithLegacyOfficeFallback( const QPoint & globalPos,
                                                     SutraMouseLookupCaptureMode captureMode,
                                                     const QString & previousClipboardText )
  {
    // Word 2010 and some 32-bit Office builds expose only a one-word range at
    // the pointer. Wait for the physical modifiers to be released before any
    // compatibility selection or clipboard operation.
    waitForPhysicalModifierRelease( [ this, globalPos, captureMode, previousClipboardText ] {
      if ( !sutraStartupIsMicrosoftOfficeWindowAtPoint( globalPos ) ) {
        lookupInProgress = false;
        return;
      }

      sendSutraStartupLeftDoubleClickAt( globalPos );

      QTimer::singleShot( 180, this, [ this, globalPos, captureMode, previousClipboardText ] {
        const QString selectedByUiAutomation = sutraStartupUiAutomationSelectedTextAtPoint( globalPos );
        const QString refreshedContext = sutraStartupUiAutomationTextAtPoint( globalPos, captureMode );

        if ( !selectedByUiAutomation.trimmed().isEmpty() ) {
          finishLegacyAutomaticCapture( globalPos,
                                        selectedByUiAutomation,
                                        refreshedContext,
                                        captureMode,
                                        previousClipboardText );
          return;
        }

        copyCurrentSelection(
          [ this, globalPos, refreshedContext, captureMode, previousClipboardText ]( const QString & copiedText ) {
            finishLegacyAutomaticCapture( globalPos,
                                          copiedText,
                                          refreshedContext,
                                          captureMode,
                                          previousClipboardText );
          } );
      } );
    } );
  }

  void lookupAutomaticText( const QPoint & globalPos,
                            SutraMouseLookupCaptureMode captureMode,
                            const QString & previousClipboardText )
  {
    // Keep the modern path completely read-only. It remains the default for
    // Office 2016/365 and every application that exposes a usable UI Automation
    // TextPattern. The legacy Office fallback runs only when this path returns
    // no usable text and the window under the pointer is Microsoft Office.
    const QString selectedText = sutraStartupUiAutomationSelectedTextAtPoint( globalPos );
    const QString contextText = sutraStartupUiAutomationTextAtPoint( globalPos, captureMode );
    const QString lookupText = automaticLookupTextFromCapture( selectedText,
                                                               contextText,
                                                               captureMode );

    if ( !lookupText.trimmed().isEmpty() ) {
      openLookup( lookupText );
      lookupInProgress = false;
      return;
    }

    if ( sutraStartupIsMicrosoftOfficeWindowAtPoint( globalPos ) ) {
      // Word can briefly return an empty RangeFromPoint while it is repaginating
      // after a font-size/layout change. Retry once after the layout settles,
      // still using the read-only UI Automation path, before any legacy
      // selection/clipboard fallback is allowed to run.
      QTimer::singleShot( 65, this, [ this, globalPos, captureMode, previousClipboardText ] {
        if ( !lookupInProgress ) {
          return;
        }

        const QString retrySelectedText = sutraStartupUiAutomationSelectedTextAtPoint( globalPos );
        const QString retryContextText = sutraStartupUiAutomationTextAtPoint( globalPos, captureMode );
        const QString retryLookupText = automaticLookupTextFromCapture( retrySelectedText,
                                                                        retryContextText,
                                                                        captureMode );

        if ( !retryLookupText.trimmed().isEmpty() ) {
          openLookup( retryLookupText );
          lookupInProgress = false;
          return;
        }

        lookupAutomaticTextWithLegacyOfficeFallback( globalPos,
                                                     captureMode,
                                                     previousClipboardText );
      } );
      return;
    }

    lookupInProgress = false;
  }

  void lookupAt( const QPoint & globalPos )
  {
    lookupInProgress = true;

    QClipboard * clipboard = QApplication::clipboard();
    if ( !clipboard ) {
      lookupInProgress = false;
      return;
    }

    const QString previousClipboardText = clipboard->text( QClipboard::Clipboard );
    const SutraMouseLookupSettings settings = loadSutraMouseLookupSettings();

    if ( settings.captureMode != SutraMouseLookupCaptureMode::SelectedText ) {
      lookupAutomaticText( globalPos, settings.captureMode, previousClipboardText );
      return;
    }

    lookupSelectedText( globalPos, previousClipboardText );
  }

  HHOOK hook = nullptr;
  bool lookupInProgress = false;
  bool suppressButtonRelease = false;
  SutraMouseLookupButton suppressedButton = SutraMouseLookupButton::Right;

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
