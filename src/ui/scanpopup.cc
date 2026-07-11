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
#include <QToolButton>
#include <QTimer>
#include <QTextDocument>
#include <QSettings>
#include <QApplication>
#include <QClipboard>
#include <QActionGroup>
#include <QPointer>
#include <functional>
#ifdef Q_OS_WIN
#include <windows.h>
#include <oleauto.h>
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <UIAutomationClient.h>
  #pragma comment( lib, "uiautomationcore.lib" )
  #pragma comment( lib, "oleaut32.lib" )
#endif
#include <QHBoxLayout>
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


void sutraForcePopupToFront( QWidget * window )
{
  if ( !window ) {
    return;
  }

  window->show();
  window->raise();
  window->activateWindow();

#ifdef Q_OS_WIN
  HWND hwnd = reinterpret_cast< HWND >( window->winId() );

  if ( hwnd ) {
    // Toggle topmost briefly so the popup is brought above the source app,
    // but do not keep it permanently Always-on-top.
    SetWindowPos( hwnd,
                  HWND_TOPMOST,
                  0,
                  0,
                  0,
                  0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW );
    SetForegroundWindow( hwnd );
    SetWindowPos( hwnd,
                  HWND_NOTOPMOST,
                  0,
                  0,
                  0,
                  0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW );
  }
#endif
}
constexpr qsizetype smartLookupMaxChars = 300;
constexpr bool smartLookupAutoPinPopup  = true;


enum class SutraPopupLayoutMode {
  Auto,
  Fixed,
  FitToResults
};

constexpr int sutraPopupFitMinWidth         = 520;
constexpr int sutraPopupFitMinHeight        = 320;
constexpr int sutraPopupFitMaxWidthPercent  = 80;
constexpr int sutraPopupFitMaxHeightPercent = 82;

QString sutraPopupLayoutModeSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupLayoutMode" );
}

QString sutraPopupFixedGeometrySettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupFixedGeometry" );
}

QString sutraPopupFontSizeSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupFontSize" );
}

constexpr int sutraPopupDefaultFontSize = 14;
constexpr int sutraPopupMinFontSize     = 11;
constexpr int sutraPopupMaxFontSize     = 36;

int loadSutraPopupFontSize()
{
  QSettings settings;
  return qBound( sutraPopupMinFontSize,
                 settings.value( sutraPopupFontSizeSettingsKey(), sutraPopupDefaultFontSize ).toInt(),
                 sutraPopupMaxFontSize );
}

void saveSutraPopupFontSize( int fontSize )
{
  QSettings settings;
  settings.setValue( sutraPopupFontSizeSettingsKey(),
                     qBound( sutraPopupMinFontSize, fontSize, sutraPopupMaxFontSize ) );
}


double sutraPopupFontZoomFactor()
{
  return qBound( 0.70,
                 static_cast< double >( loadSutraPopupFontSize() ) / static_cast< double >( sutraPopupDefaultFontSize ),
                 2.60 );
}

void applySutraPopupTextBrowserFont( QTextBrowser * browser )
{
  if ( !browser ) {
    return;
  }

  const int fontSize = loadSutraPopupFontSize();

  QFont font = browser->font();
  font.setPixelSize( fontSize );
  browser->setFont( font );

  if ( browser->document() ) {
    browser->document()->setDefaultFont( font );
  }

  browser->setStyleSheet(
    QStringLiteral( "QTextBrowser { font-size: %1px; }" ).arg( fontSize ) );
}

void applySutraPopupFontSizeToTabs( QTabWidget * tabs )
{
  if ( !tabs ) {
    return;
  }

  const int fontSize = loadSutraPopupFontSize();

  tabs->setStyleSheet(
    QStringLiteral( "QTabBar::tab { font-size: %1px; }" ).arg( qMax( 10, fontSize - 1 ) ) );

  for ( int i = 0; i < tabs->count(); ++i ) {
    if ( QTextBrowser * browser = qobject_cast< QTextBrowser * >( tabs->widget( i ) ) ) {
      applySutraPopupTextBrowserFont( browser );
    }
  }
}
QString sutraPopupOpacitySettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupOpacityPercent" );
}

constexpr int sutraPopupDefaultOpacityPercent = 100;
constexpr int sutraPopupMinOpacityPercent     = 75;
constexpr int sutraPopupMaxOpacityPercent     = 100;

int loadSutraPopupOpacityPercent()
{
  QSettings settings;
  return qBound( sutraPopupMinOpacityPercent,
                 settings.value( sutraPopupOpacitySettingsKey(), sutraPopupDefaultOpacityPercent ).toInt(),
                 sutraPopupMaxOpacityPercent );
}

void saveSutraPopupOpacityPercent( int opacityPercent )
{
  QSettings settings;
  settings.setValue( sutraPopupOpacitySettingsKey(),
                     qBound( sutraPopupMinOpacityPercent, opacityPercent, sutraPopupMaxOpacityPercent ) );
}


enum class SutraMouseLookupMode {
  Disabled       = 0,
  CtrlRightClick = 1,
  CtrlLeftClick  = 2,
  AltRightClick  = 3
};

QString sutraMouseLookupModeSettingsKey()
{
  return QStringLiteral( "SutraEdition/MouseLookupMode" );
}

constexpr int sutraMouseLookupDefaultMode = static_cast< int >( SutraMouseLookupMode::CtrlRightClick );

SutraMouseLookupMode loadSutraMouseLookupMode()
{
  QSettings settings;
  const int value =
    qBound( 0, settings.value( sutraMouseLookupModeSettingsKey(), sutraMouseLookupDefaultMode ).toInt(), 3 );
  return static_cast< SutraMouseLookupMode >( value );
}

void saveSutraMouseLookupMode( SutraMouseLookupMode mode )
{
  QSettings settings;
  settings.setValue( sutraMouseLookupModeSettingsKey(), static_cast< int >( mode ) );
}

QString sutraMouseLookupModeLabel( SutraMouseLookupMode mode )
{
  switch ( mode ) {
    case SutraMouseLookupMode::Disabled:
      return QStringLiteral( "Disabled" );
    case SutraMouseLookupMode::CtrlLeftClick:
      return QStringLiteral( "Ctrl + Left Click" );
    case SutraMouseLookupMode::AltRightClick:
      return QStringLiteral( "Alt + Right Click" );
    case SutraMouseLookupMode::CtrlRightClick:
    default:
      return QStringLiteral( "Ctrl + Right Click" );
  }
}

void resetSutraPopupAppearanceDefaults()
{
  QSettings settings;
  settings.setValue( sutraPopupLayoutModeSettingsKey(), QStringLiteral( "auto" ) );
  settings.remove( sutraPopupFixedGeometrySettingsKey() );
  settings.setValue( sutraPopupFontSizeSettingsKey(), sutraPopupDefaultFontSize );
  settings.setValue( sutraPopupOpacitySettingsKey(), sutraPopupDefaultOpacityPercent );
  settings.setValue( sutraMouseLookupModeSettingsKey(), sutraMouseLookupDefaultMode );
}

void applySutraPopupOpacity( QWidget * popup )
{
  if ( !popup ) {
    return;
  }

  popup->setWindowOpacity( loadSutraPopupOpacityPercent() / 100.0 );
}

#ifdef Q_OS_WIN

#include <windows.h>
#include <oleauto.h>
void sendSutraVirtualKey( WORD virtualKey, bool down )
{
  INPUT input  = {};
  input.type   = INPUT_KEYBOARD;
  input.ki.wVk = virtualKey;

  if ( !down ) {
    input.ki.dwFlags = KEYEVENTF_KEYUP;
  }

  SendInput( 1, &input, sizeof( INPUT ) );
}

void releaseSutraControlKeys()
{
  sendSutraVirtualKey( VK_CONTROL, false );
  sendSutraVirtualKey( VK_LCONTROL, false );
  sendSutraVirtualKey( VK_RCONTROL, false );
}


void releaseSutraAltKeys()
{
  sendSutraVirtualKey( VK_MENU, false );
  sendSutraVirtualKey( VK_LMENU, false );
  sendSutraVirtualKey( VK_RMENU, false );
}

void sendSutraCtrlC()
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

void sendSutraLeftDoubleClickAt( const QPoint & globalPos )
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

QString sutraTextFromBstr( BSTR text )
{
  if ( !text ) {
    return {};
  }

  QString result = QString::fromWCharArray( text, static_cast< int >( SysStringLen( text ) ) );
  SysFreeString( text );
  return result;
}

QString sutraTextFromUiAutomationRange( IUIAutomationTextRange * range )
{
  if ( !range ) {
    return {};
  }

  // A single CJK character is often not enough. Capture the surrounding line,
  // then the smart glossary will choose the longest matching Buddhist term.
  range->ExpandToEnclosingUnit( TextUnit_Line );

  BSTR text = nullptr;
  if ( FAILED( range->GetText( 600, &text ) ) ) {
    return {};
  }

  return sutraTextFromBstr( text );
}

QString sutraUiAutomationTextAtPoint( const QPoint & globalPos )
{
  HRESULT coInitResult             = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
  const bool shouldUninitializeCom = SUCCEEDED( coInitResult );

  IUIAutomation * automation = nullptr;
  HRESULT hr = CoCreateInstance( CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &automation ) );

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
  hr                             = automation->ElementFromPoint( point, &element );

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

    hr = currentElement->GetCurrentPatternAs( UIA_TextPatternId, IID_PPV_ARGS( &textPattern ) );

    if ( SUCCEEDED( hr ) && textPattern ) {
      IUIAutomationTextRange * textRange = nullptr;
      hr                                 = textPattern->RangeFromPoint( point, &textRange );

      if ( SUCCEEDED( hr ) && textRange ) {
        result = sutraTextFromUiAutomationRange( textRange );
        textRange->Release();
      }

      textPattern->Release();
    }

    if ( !result.trimmed().isEmpty() || !walker ) {
      break;
    }

    IUIAutomationElement * parentElement = nullptr;
    hr                                   = walker->GetParentElement( currentElement, &parentElement );

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

  return result;
}

class SutraCtrlRightClickLookupHook final
{
public:
  void setCallback( std::function< void( QPoint ) > callback_ )
  {
    callback = std::move( callback_ );
  }

  bool ensureInstalled()
  {
    // Disabled here because startup hook in main.cc owns mouse lookup from app launch.
    return false;

    if ( hook ) {
      return true;
    }

    instance = this;
    hook = SetWindowsHookExW( WH_MOUSE_LL, &SutraCtrlRightClickLookupHook::mouseProc, GetModuleHandleW( nullptr ), 0 );

    if ( !hook ) {
      qWarning() << "Unable to install Sutra mouse lookup hook. Error:" << GetLastError();
      return false;
    }

    return true;
  }

  ~SutraCtrlRightClickLookupHook()
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
    return ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) || ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 )
      || ( GetAsyncKeyState( VK_RCONTROL ) & 0x8000 );
  }

  static bool isAltPressed()
  {
    return ( GetAsyncKeyState( VK_MENU ) & 0x8000 ) || ( GetAsyncKeyState( VK_LMENU ) & 0x8000 )
      || ( GetAsyncKeyState( VK_RMENU ) & 0x8000 );
  }

  static bool isMouseLookupDownEvent( SutraMouseLookupMode mode, WPARAM wParam )
  {
    switch ( mode ) {
      case SutraMouseLookupMode::CtrlLeftClick:
        return wParam == WM_LBUTTONDOWN;
      case SutraMouseLookupMode::AltRightClick:
      case SutraMouseLookupMode::CtrlRightClick:
        return wParam == WM_RBUTTONDOWN;
      case SutraMouseLookupMode::Disabled:
      default:
        return false;
    }
  }

  static bool shouldHandleMouseLookupEvent( SutraMouseLookupMode mode, WPARAM wParam )
  {
    switch ( mode ) {
      case SutraMouseLookupMode::CtrlRightClick:
        return isControlPressed() && ( wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP );
      case SutraMouseLookupMode::CtrlLeftClick:
        return isControlPressed() && ( wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP );
      case SutraMouseLookupMode::AltRightClick:
        return isAltPressed() && ( wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP );
      case SutraMouseLookupMode::Disabled:
      default:
        return false;
    }
  }

  static LRESULT CALLBACK mouseProc( int code, WPARAM wParam, LPARAM lParam )
  {
    if ( code == HC_ACTION && instance ) {
      const SutraMouseLookupMode mode = loadSutraMouseLookupMode();

      if ( shouldHandleMouseLookupEvent( mode, wParam ) ) {
        if ( isMouseLookupDownEvent( mode, wParam ) && instance->callback ) {
          const MSLLHOOKSTRUCT * mouseInfo = reinterpret_cast< const MSLLHOOKSTRUCT * >( lParam );
          const QPoint globalPos( mouseInfo->pt.x, mouseInfo->pt.y );
          const auto callbackCopy = instance->callback;

          QTimer::singleShot( 0, qApp, [ callbackCopy, globalPos ] {
            callbackCopy( globalPos );
          } );
        }

        // Suppress the configured click so external apps do not open their own menu or selection action.
        return 1;
      }
    }

    return CallNextHookEx( instance ? instance->hook : nullptr, code, wParam, lParam );
  }

  HHOOK hook = nullptr;
  std::function< void( QPoint ) > callback;

  static SutraCtrlRightClickLookupHook * instance;
};

SutraCtrlRightClickLookupHook * SutraCtrlRightClickLookupHook::instance = nullptr;

SutraCtrlRightClickLookupHook * sutraCtrlRightClickLookupHook()
{
  static SutraCtrlRightClickLookupHook * hook = nullptr;

  if ( !hook ) {
    hook = new SutraCtrlRightClickLookupHook;
  }

  return hook;
}

bool registerSutraCtrlRightClickLookup( std::function< void( QPoint ) > callback )
{
  SutraCtrlRightClickLookupHook * hook = sutraCtrlRightClickLookupHook();
  hook->setCallback( std::move( callback ) );
  return hook->ensureInstalled();
}

#endif

QString sutraPopupCornerToolsObjectName()
{
  return QStringLiteral( "sutraPopupCornerTools" );
}

void positionSutraPopupCornerTools( QWidget * popup )
{
  if ( !popup ) {
    return;
  }

  QWidget * tools = popup->findChild< QWidget * >( sutraPopupCornerToolsObjectName() );
  if ( !tools ) {
    return;
  }

  tools->adjustSize();

  constexpr int margin = 10;
  const int x          = qMax( margin, popup->width() - tools->width() - margin );
  const int y          = qMax( margin, popup->height() - tools->height() - margin );

  tools->move( x, y );
  tools->raise();
}

QString sutraPopupLayoutModeToString( SutraPopupLayoutMode mode )
{
  switch ( mode ) {
    case SutraPopupLayoutMode::Fixed:
      return QStringLiteral( "fixed" );
    case SutraPopupLayoutMode::FitToResults:
      return QStringLiteral( "fit" );
    case SutraPopupLayoutMode::Auto:
    default:
      return QStringLiteral( "auto" );
  }
}

SutraPopupLayoutMode sutraPopupLayoutModeFromString( const QString & value )
{
  const QString normalized = value.trimmed().toLower();

  if ( normalized == QStringLiteral( "fixed" ) ) {
    return SutraPopupLayoutMode::Fixed;
  }

  if ( normalized == QStringLiteral( "fit" ) ) {
    return SutraPopupLayoutMode::FitToResults;
  }

  return SutraPopupLayoutMode::Auto;
}

SutraPopupLayoutMode loadSutraPopupLayoutMode()
{
  QSettings settings;
  return sutraPopupLayoutModeFromString(
    settings.value( sutraPopupLayoutModeSettingsKey(), QStringLiteral( "auto" ) ).toString() );
}

void saveSutraPopupLayoutMode( SutraPopupLayoutMode mode )
{
  QSettings settings;
  settings.setValue( sutraPopupLayoutModeSettingsKey(), sutraPopupLayoutModeToString( mode ) );
}

QRect safeSutraPopupGeometry( QRect geometry )
{
  QScreen * screen = QGuiApplication::screenAt( geometry.center() );

  if ( !screen ) {
    screen = QGuiApplication::primaryScreen();
  }

  if ( !screen ) {
    return geometry;
  }

  const QRect available = screen->availableGeometry();

  const int maxWidth  = qMax( sutraPopupFitMinWidth, available.width() * sutraPopupFitMaxWidthPercent / 100 );
  const int maxHeight = qMax( sutraPopupFitMinHeight, available.height() * sutraPopupFitMaxHeightPercent / 100 );

  geometry.setWidth( qBound( sutraPopupFitMinWidth, geometry.width(), maxWidth ) );
  geometry.setHeight( qBound( sutraPopupFitMinHeight, geometry.height(), maxHeight ) );

  if ( geometry.right() > available.right() ) {
    geometry.moveRight( available.right() );
  }
  if ( geometry.bottom() > available.bottom() ) {
    geometry.moveBottom( available.bottom() );
  }
  if ( geometry.left() < available.left() ) {
    geometry.moveLeft( available.left() );
  }
  if ( geometry.top() < available.top() ) {
    geometry.moveTop( available.top() );
  }

  return geometry;
}

void saveSutraPopupFixedGeometry( QWidget * popup )
{
  if ( !popup ) {
    return;
  }

  QSettings settings;
  settings.setValue( sutraPopupFixedGeometrySettingsKey(), popup->geometry() );
  saveSutraPopupLayoutMode( SutraPopupLayoutMode::Fixed );
}

QRect loadSutraPopupFixedGeometry( QWidget * popup )
{
  QSettings settings;
  QRect geometry = settings.value( sutraPopupFixedGeometrySettingsKey() ).toRect();

  if ( !geometry.isValid() && popup ) {
    geometry = popup->geometry();
  }

  if ( !geometry.isValid() ) {
    geometry = QRect( 120, 120, 760, 520 );
  }

  return safeSutraPopupGeometry( geometry );
}

QSize fitSutraPopupSizeFromCurrentTab( QWidget * popup, QTabWidget * tabs )
{
  QSize target = popup ? popup->size() : QSize( 760, 520 );

  if ( target.width() < 700 ) {
    target.setWidth( 700 );
  }
  if ( target.height() < 460 ) {
    target.setHeight( 460 );
  }

  if ( tabs ) {
    if ( QTextBrowser * browser = qobject_cast< QTextBrowser * >( tabs->currentWidget() ) ) {
      const QSize documentSize = browser->document()->size().toSize();
      if ( documentSize.isValid() ) {
        target.setWidth( qMax( target.width(), documentSize.width() + 80 ) );
        target.setHeight( qMax( sutraPopupFitMinHeight, documentSize.height() + 190 ) );
      }
    }
    else if ( tabs->currentWidget() ) {
      const QSize hint = tabs->currentWidget()->sizeHint();
      if ( hint.isValid() ) {
        target.setWidth( qMax( target.width(), hint.width() + 80 ) );
        target.setHeight( qMax( target.height(), hint.height() + 140 ) );
      }
    }
  }

  return target;
}

void fitSutraPopupToResults( QWidget * popup, QTabWidget * tabs )
{
  if ( !popup ) {
    return;
  }

  QRect geometry = popup->geometry();
  geometry.setSize( fitSutraPopupSizeFromCurrentTab( popup, tabs ) );
  popup->setGeometry( safeSutraPopupGeometry( geometry ) );
}

void applySutraPopupLayoutMode( QWidget * popup, QTabWidget * tabs )
{
  switch ( loadSutraPopupLayoutMode() ) {
    case SutraPopupLayoutMode::Fixed:
      if ( popup ) {
        popup->setGeometry( loadSutraPopupFixedGeometry( popup ) );
      }
      break;
    case SutraPopupLayoutMode::FitToResults:
      fitSutraPopupToResults( popup, tabs );
      break;
    case SutraPopupLayoutMode::Auto:
    default:
      break;
  }
}

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

QString chooseCtrlRightClickLookupText( const QString & capturedText, const QString & contextText )
{
  const QString captured = normalizeSmartLookupInput( capturedText ).trimmed();
  const QString context  = normalizeSmartLookupInput( contextText ).trimmed();

  if ( captured.isEmpty() && context.isEmpty() ) {
    return {};
  }

  if ( containsCjkText( context ) ) {
    const QString compactCaptured   = compactCjkText( captured );
    const QStringList detectedTerms = detectSmartLookupTerms( context );

    QString bestTerm;
    int bestLength = -1;

    for ( const QString & term : detectedTerms ) {
      const QString compactTerm = compactCjkText( term );

      if ( compactTerm.isEmpty() ) {
        continue;
      }

      if ( !compactCaptured.isEmpty() ) {
        // If double-click captured only one CJK character, choose the longest
        // glossary term in the surrounding line that contains that character.
        if ( compactCaptured.size() <= 2 ) {
          if ( !compactTerm.contains( compactCaptured ) ) {
            continue;
          }
        }
        else if ( compactTerm != compactCaptured && !compactTerm.contains( compactCaptured )
                  && !compactCaptured.contains( compactTerm ) ) {
          continue;
        }
      }

      if ( compactTerm.size() > bestLength ) {
        bestTerm   = term;
        bestLength = compactTerm.size();
      }
    }

    if ( !bestTerm.isEmpty() ) {
      return bestTerm;
    }

    if ( !detectedTerms.isEmpty() ) {
      return chooseSmartLookupQuery( context, detectedTerms );
    }
  }

  if ( !captured.isEmpty() ) {
    return captured;
  }

  if ( !context.isEmpty() ) {
    const QStringList detectedTerms = detectSmartLookupTerms( context );
    return chooseSmartLookupQuery( context, detectedTerms );
  }

  return {};
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

QString sutraHtmlLabelPhatHocV3()
{
  return QStringLiteral( "Ph&#7853;t h&#7885;c" );
}

QString sutraGlossaryChipHtmlV5( const QString & text )
{
  if ( text.trimmed().isEmpty() ) {
    return {};
  }

  return QStringLiteral( "<span class='chip'>%1</span>" ).arg( htmlEscape( text.trimmed() ) );
}

QString sutraGlossaryCardHtmlV5( const BuddhistGlossaryEntry & entry, bool isPrimary )
{
  QString html;

  html += QStringLiteral( "<article class='term-card %1'>" )
            .arg( isPrimary ? QStringLiteral( "primary-card" ) : QStringLiteral( "related-card" ) );

  html += QStringLiteral( "<div class='card-top'>" );
  html += QStringLiteral( "<div class='term-block'>" );
  html += QStringLiteral( "<div class='term-title'>%1</div>" ).arg( htmlEscape( entry.term ) );

  if ( !entry.hanViet.isEmpty() ) {
    html += QStringLiteral( "<div class='term-subtitle'>%1</div>" ).arg( htmlEscape( entry.hanViet ) );
  }

  html += QStringLiteral( "</div>" );

  if ( !entry.category.isEmpty() ) {
    html += QStringLiteral( "<div class='category-badge'>%1</div>" ).arg( htmlEscape( entry.category ) );
  }

  html += QStringLiteral( "</div>" );

  if ( !entry.pinyin.isEmpty() ) {
    html += QStringLiteral(
      "<div class='meta-row'><span class='meta-label'>Pinyin</span><span class='meta-value latin'>%1</span></div>" )
              .arg( htmlEscape( entry.pinyin ) );
  }

  if ( !entry.meaningVi.isEmpty() ) {
    html += QStringLiteral(
      "<section class='meaning-box'>"
      "<div class='section-label'>Ngh&#297;a ti&#7871;ng Vi&#7879;t</div>"
      "<div class='meaning-text'>%1</div>"
      "</section>" )
              .arg( htmlEscape( entry.meaningVi ) );
  }

  if ( !entry.suggestedTranslations.isEmpty() ) {
    QStringList suggestionChips;

    for ( const QString & suggestion : entry.suggestedTranslations ) {
      suggestionChips << sutraGlossaryChipHtmlV5( suggestion );
    }

    html += QStringLiteral(
      "<section class='mini-section'>"
      "<div class='section-label'>G&#7907;i &#253; d&#7883;ch</div>"
      "<div class='chip-row'>%1</div>"
      "</section>" )
              .arg( suggestionChips.join( QString() ) );
  }

  if ( !entry.related.isEmpty() ) {
    QStringList relatedChips;

    for ( const QString & related : entry.related ) {
      relatedChips << sutraGlossaryChipHtmlV5( related );
    }

    html += QStringLiteral(
      "<section class='mini-section'>"
      "<div class='section-label'>Li&#234;n quan</div>"
      "<div class='chip-row related-chips'>%1</div>"
      "</section>" )
              .arg( relatedChips.join( QString() ) );
  }

  html += QStringLiteral( "</article>" );
  return html;
}

QString glossaryEntryHtml( const BuddhistGlossaryEntry & entry )
{
  return sutraGlossaryCardHtmlV5( entry, false );
}

QString glossaryHtml( const QString & primaryTerm, const QStringList & detectedTerms )
{
  const QList< BuddhistGlossaryEntry > entries = glossaryEntriesForTerms( primaryTerm, detectedTerms );
  const int fontSize                           = loadSutraPopupFontSize();
  const int titleSize                          = qMax( 24, fontSize + 10 );
  const int cardTermSize                       = qMax( 26, fontSize + 12 );
  const int smallSize                          = qMax( 11, fontSize - 2 );

  QString html;
  html += QStringLiteral( "<html><head><meta charset='utf-8'>" );
  html += QStringLiteral(
    "<style>"
    "body{font-family:'Segoe UI','Noto Sans','Arial',sans-serif;font-size:%1px;line-height:1.58;margin:0;padding:14px;background:#f6f8fb;color:#1f2937;}"
    ".wrap{max-width:980px;margin:0 auto;}"
    ".hero{background:linear-gradient(135deg,#eef6ff 0%,#f8fbff 100%);border:1px solid #d7e7fb;border-radius:14px;padding:14px 16px;margin-bottom:12px;}"
    ".hero-title{font-size:%2px;font-weight:800;color:#111827;margin:0 0 4px 0;}"
    ".hero-subtitle{color:#64748b;margin:0;font-size:%3px;}"
    ".section-title{font-size:%4px;font-weight:800;color:#334155;margin:14px 0 8px 2px;display:flex;align-items:center;gap:8px;}"
    ".section-title:before{content:'';display:inline-block;width:4px;height:18px;border-radius:999px;background:#2563eb;}"
    ".term-card{background:#ffffff;border:1px solid #e2e8f0;border-radius:14px;padding:14px 16px;margin:0 0 12px 0;box-shadow:0 4px 14px rgba(15,23,42,.06);}"
    ".primary-card{border-color:#bfdbfe;box-shadow:0 6px 18px rgba(37,99,235,.10);}"
    ".related-card{background:#ffffff;}"
    ".card-top{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:8px;}"
    ".term-title{font-size:%5px;font-weight:850;line-height:1.18;color:#020617;letter-spacing:.2px;}"
    ".term-subtitle{font-size:%6px;color:#0f766e;font-weight:700;margin-top:4px;}"
    ".category-badge{display:inline-block;white-space:nowrap;background:#ecfdf5;color:#047857;border:1px solid #bbf7d0;border-radius:999px;font-size:%3px;font-weight:700;padding:4px 9px;}"
    ".meta-row{display:flex;gap:10px;margin:6px 0;color:#334155;}"
    ".meta-label,.section-label{font-size:%3px;font-weight:800;letter-spacing:.04em;text-transform:uppercase;color:#64748b;}"
    ".meta-value{font-weight:650;color:#111827;}"
    ".latin{font-family:'Segoe UI','Arial',sans-serif;}"
    ".meaning-box{margin-top:10px;padding:12px;border-radius:12px;background:#f8fafc;border:1px solid #e2e8f0;}"
    ".meaning-text{margin-top:5px;color:#111827;font-weight:500;}"
    ".mini-section{margin-top:10px;}"
    ".chip-row{margin-top:7px;display:flex;flex-wrap:wrap;gap:6px;}"
    ".chip{display:inline-block;background:#f1f5f9;border:1px solid #e2e8f0;color:#334155;border-radius:999px;padding:4px 9px;font-weight:650;}"
    ".related-chips .chip{background:#fff7ed;border-color:#fed7aa;color:#9a3412;}"
    ".empty-card{background:#ffffff;border:1px dashed #cbd5e1;border-radius:14px;padding:18px;box-shadow:0 4px 14px rgba(15,23,42,.05);}"
    ".empty-title{font-size:%4px;font-weight:800;color:#111827;margin-bottom:6px;}"
    ".empty-text{color:#64748b;margin-bottom:10px;}"
    ".tips{margin:10px 0 0 0;padding-left:18px;color:#475569;}"
    ".source{color:#94a3b8;font-size:%3px;margin:14px 2px 2px 2px;}"
    "</style></head><body><div class='wrap'>" )
      .arg( fontSize )
      .arg( titleSize )
      .arg( smallSize )
      .arg( qMax( 18, fontSize + 2 ) )
      .arg( cardTermSize )
      .arg( qMax( 17, fontSize + 2 ) );

  html += QStringLiteral(
    "<div class='hero'>"
    "<div class='hero-title'>Ph&#7853;t h&#7885;c / Buddhist Glossary</div>"
    "<p class='hero-subtitle'>K&#7871;t qu&#7843; nh&#7853;n di&#7879;n t&#7915; buddhist_terms.json, &#432;u ti&#234;n c&#7909;m thu&#7853;t ng&#7919; ch&#237;nh tr&#432;&#7899;c r&#7891;i m&#7899;i &#273;&#7871;n t&#7915; li&#234;n quan.</p>"
    "</div>" );

  if ( entries.isEmpty() ) {
    html += QStringLiteral(
      "<div class='empty-card'>"
      "<div class='empty-title'>Ch&#432;a c&#243; thu&#7853;t ng&#7919; ph&#249; h&#7907;p</div>"
      "<div class='empty-text'>Kh&#244;ng t&#236;m th&#7845;y m&#7909;c Ph&#7853;t h&#7885;c n&#7897;i b&#7897; cho: <b>%1</b></div>"
      "<ul class='tips'>"
      "<li>Th&#7917; tra c&#7909;m ng&#7855;n h&#417;n ho&#7863;c ch&#7885;n &#273;&#250;ng c&#7909;m H&#225;n/Vi&#7879;t.</li>"
      "<li>N&#7871;u &#273;&#226;y l&#224; thu&#7853;t ng&#7919; c&#7847;n d&#249;ng th&#432;&#7901;ng xuy&#234;n, h&#227;y b&#7893; sung v&#224;o <b>buddhist_terms.json</b>.</li>"
      "<li>C&#243; th&#7875; xem tab <b>Web</b> &#273;&#7875; tham kh&#7843;o ngu&#7891;n ngo&#224;i.</li>"
      "</ul>"
      "</div>" )
        .arg( htmlEscape( primaryTerm ) );
  }
  else {
    QList< BuddhistGlossaryEntry > primaryEntries;
    QList< BuddhistGlossaryEntry > relatedEntries;

    const QString normalizedPrimary = normalizeSmartLookupInput( primaryTerm );

    for ( const BuddhistGlossaryEntry & entry : entries ) {
      if ( normalizeSmartLookupInput( entry.term ) == normalizedPrimary ) {
        primaryEntries << entry;
      }
      else {
        relatedEntries << entry;
      }
    }

    if ( primaryEntries.isEmpty() && !entries.isEmpty() ) {
      primaryEntries << entries.first();

      for ( int i = 1; i < entries.size(); ++i ) {
        relatedEntries << entries.at( i );
      }
    }

    html += QStringLiteral( "<div class='section-title'>K&#7871;t qu&#7843; ch&#237;nh</div>" );

    for ( const BuddhistGlossaryEntry & entry : primaryEntries ) {
      html += sutraGlossaryCardHtmlV5( entry, true );
    }

    if ( !relatedEntries.isEmpty() ) {
      html += QStringLiteral( "<div class='section-title'>T&#7915; li&#234;n quan</div>" );

      for ( const BuddhistGlossaryEntry & entry : relatedEntries ) {
        html += sutraGlossaryCardHtmlV5( entry, false );
      }
    }
  }

  html += QStringLiteral( "<p class='source'>Ngu&#7891;n d&#7919; li&#7879;u: buddhist_terms.json</p>" );
  html += QStringLiteral( "</div></body></html>" );
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


  QTextBrowser * browser = findBuddhistGlossaryBrowser( tabs );
  if ( !browser ) {
    browser = new QTextBrowser( tabs );
    browser->setObjectName( QStringLiteral( "buddhistGlossaryBrowser" ) );
    browser->setOpenExternalLinks( true );
    tabs->addTab( browser, QStringLiteral( "Phật học" ) );
  }

  browser->setHtml( glossaryHtml( primaryTerm, detectedTerms ) );
  applySutraPopupTextBrowserFont( browser );
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
  const int fontSize      = loadSutraPopupFontSize();

  QString html;
  html += QStringLiteral( "<html><head><meta charset='utf-8'>" );
  html += QStringLiteral(
    "<style>"
    "body{font-family:'Segoe UI','Noto Sans','Arial',sans-serif;font-size:%1px;line-height:1.55;margin:0;padding:14px;background:#f6f8fb;color:#1f2937;}"
    ".wrap{max-width:980px;margin:0 auto;}"
    ".hero{background:#fff;border:1px solid #e2e8f0;border-radius:14px;padding:14px 16px;margin-bottom:12px;box-shadow:0 4px 14px rgba(15,23,42,.05);}"
    ".title{font-size:%2px;font-weight:850;margin-bottom:4px;color:#111827;}"
    ".subtitle{color:#64748b;margin:0;}"
    ".term-card{background:#fff;border:1px solid #e2e8f0;border-radius:12px;padding:12px 14px;margin:0 0 10px 0;}"
    ".term{font-size:%3px;font-weight:800;margin-bottom:8px;color:#111827;}"
    ".links{margin:0;padding-left:20px;}"
    ".links li{margin:5px 0;}"
    "a{color:#2563eb;text-decoration:none;font-weight:650;}"
    "a:hover{text-decoration:underline;}"
    ".empty{background:#fff;border:1px dashed #cbd5e1;border-radius:14px;padding:18px;color:#64748b;}"
    ".source{color:#94a3b8;font-size:%4px;margin-top:12px;}"
    "</style></head><body><div class='wrap'>" )
      .arg( fontSize )
      .arg( qMax( 22, fontSize + 6 ) )
      .arg( qMax( 18, fontSize + 3 ) )
      .arg( qMax( 11, fontSize - 2 ) );

  html += QStringLiteral(
    "<div class='hero'>"
    "<div class='title'>Web Reference</div>"
    "<p class='subtitle'>Ngu&#7891;n ngo&#224;i &#273;&#7875; tham kh&#7843;o nhanh, kh&#244;ng thay th&#7871; glossary n&#7897;i b&#7897;.</p>"
    "</div>" );

  if ( terms.isEmpty() ) {
    html += QStringLiteral(
      "<div class='empty'>Ch&#432;a c&#243; t&#7915; kh&#243;a &#273;&#7875; t&#7841;o li&#234;n k&#7871;t tham kh&#7843;o. H&#227;y th&#7917; tra l&#7841;i b&#7857;ng m&#7897;t thu&#7853;t ng&#7919; c&#7909; th&#7875; h&#417;n.</div>" );
  }
  else {
    for ( const QString & term : terms ) {
      const QString encoded = webReferenceUrlEncode( term );

      html += QStringLiteral( "<div class='term-card'>" );
      html += QStringLiteral( "<div class='term'>%1</div>" ).arg( htmlEscape( term ) );
      html += QStringLiteral( "<ul class='links'>" );
      html += webReferenceLinkHtml( QStringLiteral( "Wikipedia ti&#7871;ng Trung" ),
                                    QStringLiteral( "https://zh.wikipedia.org/wiki/%1" ).arg( encoded ) );
      html += webReferenceLinkHtml( QStringLiteral( "Wikipedia search ti&#7871;ng Anh" ),
                                    QStringLiteral( "https://en.wikipedia.org/w/index.php?search=%1" ).arg( encoded ) );
      html += webReferenceLinkHtml( QStringLiteral( "Wiktionary" ),
                                    QStringLiteral( "https://en.wiktionary.org/wiki/%1" ).arg( encoded ) );
      html += webReferenceLinkHtml( QStringLiteral( "Google Search" ),
                                    QStringLiteral( "https://www.google.com/search?q=%1" ).arg( encoded ) );
      html += QStringLiteral( "</ul></div>" );
    }
  }

  html += QStringLiteral( "<div class='source'>G&#7907;i &#253;: d&#249;ng c&#225;c ngu&#7891;n web nh&#432; t&#224;i li&#7879;u tham kh&#7843;o.</div>" );
  html += QStringLiteral( "</div></body></html>" );
  return html;
}

QTextBrowser * findWebReferenceBrowser( QTabWidget * tabs )
{
  if ( !tabs ) {
    return nullptr;
  }

  for ( int i = 0; i < tabs->count(); ++i ) {
    if ( tabs->tabText( i ) == QObject::tr( "Web" ) ) {
      return qobject_cast< QTextBrowser * >( tabs->widget( i ) );
    }
  }

  return nullptr;
}
void removeWebReferenceTab( QTabWidget * tabs )
{
  if ( !tabs ) {
    return;
  }

  for ( int i = tabs->count() - 1; i >= 0; --i ) {
    if ( tabs->tabText( i ) == QObject::tr( "Web" ) ) {
      QWidget * widget = tabs->widget( i );
      tabs->removeTab( i );

      if ( widget ) {
        widget->deleteLater();
      }
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
  applySutraPopupTextBrowserFont( browser );
}

void refreshSutraCustomTabs( QTabWidget * tabs, const QString & primaryTerm, const QString & currentInputText )
{
  QString lookupText = normalizeSmartLookupInput( Folding::unescapeWildcardSymbols( currentInputText ) );

  if ( lookupText.isEmpty() ) {
    lookupText = primaryTerm;
  }

  const QStringList smartTerms = detectSmartLookupTerms( lookupText );
  QString smartQuery           = chooseSmartLookupQuery( lookupText, smartTerms );

  if ( smartQuery.isEmpty() ) {
    smartQuery = primaryTerm;
  }

  updateBuddhistGlossaryTab( tabs, smartQuery, smartTerms );
  updateWebReferenceTab( tabs, smartQuery, smartTerms );

  if ( QTextBrowser * glossaryBrowser = findBuddhistGlossaryBrowser( tabs ) ) {
    const int index = tabs->indexOf( glossaryBrowser );
    if ( index >= 0 ) {
      tabs->setCurrentIndex( index );
    }
  }
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


  ui.pinButton->setToolTip( ui.pinButton->toolTip() + tr( "\nClick: popup options / tùy chọn popup" ) );

  QMenu * sutraPopupLayoutMenu = new QMenu( tr( "Popup options" ), ui.pinButton );

  QAction * sutraPopupPinAction = sutraPopupLayoutMenu->addAction( tr( "Pin / keep popup open" ) );
  sutraPopupPinAction->setCheckable( true );

  sutraPopupLayoutMenu->addSeparator();

  QActionGroup * sutraPopupLayoutGroup = new QActionGroup( sutraPopupLayoutMenu );

  QAction * sutraPopupAutoAction  = sutraPopupLayoutMenu->addAction( tr( "Auto - let GoldenDict decide" ) );
  QAction * sutraPopupFixedAction = sutraPopupLayoutMenu->addAction( tr( "Fix current size and position" ) );
  QAction * sutraPopupFitAction   = sutraPopupLayoutMenu->addAction( tr( "Fit window size to results" ) );

  for ( QAction * action : { sutraPopupAutoAction, sutraPopupFixedAction, sutraPopupFitAction } ) {
    action->setCheckable( true );
    sutraPopupLayoutGroup->addAction( action );
  }

  sutraPopupLayoutMenu->addSeparator();

  QMenu * sutraPopupFontSizeMenu         = sutraPopupLayoutMenu->addMenu( tr( "Font size" ) );
  QActionGroup * sutraPopupFontSizeGroup = new QActionGroup( sutraPopupFontSizeMenu );
  QList< QAction * > sutraPopupFontSizeActions;

  for ( int fontSize : { 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 32, 36 } ) {
    QAction * fontAction = sutraPopupFontSizeMenu->addAction( tr( "%1 px" ).arg( fontSize ) );
    fontAction->setCheckable( true );
    fontAction->setData( fontSize );
    sutraPopupFontSizeGroup->addAction( fontAction );
    sutraPopupFontSizeActions << fontAction;

    connect( fontAction, &QAction::triggered, this, [ this, fontSize ] {
      saveSutraPopupFontSize( fontSize );
      applyZoomFactor();
      refreshSutraCustomTabs( tabWidget, pendingWord, translateBox->translateLine()->text() );
      applySutraPopupFontSizeToTabs( tabWidget );
      if ( loadSutraPopupLayoutMode() == SutraPopupLayoutMode::FitToResults ) {
        fitSutraPopupToResults( this, tabWidget );
      }
      showStatusBarMessage( tr( "Popup font size: %1 px" ).arg( fontSize ), 4000 );
    } );
  }


  QMenu * sutraPopupTransparencyMenu         = sutraPopupLayoutMenu->addMenu( tr( "Transparency" ) );
  QActionGroup * sutraPopupTransparencyGroup = new QActionGroup( sutraPopupTransparencyMenu );
  QList< QAction * > sutraPopupTransparencyActions;

  for ( int opacityPercent : { 75, 80, 85, 90, 95, 100 } ) {
    QAction * opacityAction = sutraPopupTransparencyMenu->addAction( tr( "%1%" ).arg( opacityPercent ) );
    opacityAction->setCheckable( true );
    opacityAction->setData( opacityPercent );
    sutraPopupTransparencyGroup->addAction( opacityAction );
    sutraPopupTransparencyActions << opacityAction;

    connect( opacityAction, &QAction::triggered, this, [ this, opacityPercent ] {
      saveSutraPopupOpacityPercent( opacityPercent );
      applySutraPopupOpacity( this );
      showStatusBarMessage( tr( "Popup transparency: %1%" ).arg( opacityPercent ), 4000 );
    } );
  }

  sutraPopupLayoutMenu->addSeparator();

  sutraPopupLayoutMenu->addSeparator();

  QMenu * sutraMouseLookupMenu         = sutraPopupLayoutMenu->addMenu( tr( "Mouse lookup" ) );
  QActionGroup * sutraMouseLookupGroup = new QActionGroup( sutraMouseLookupMenu );
  QList< QAction * > sutraMouseLookupActions;

  auto addSutraMouseLookupAction = [ & ]( const QString & label, SutraMouseLookupMode mode ) {
    QAction * action = sutraMouseLookupMenu->addAction( label );
    action->setCheckable( true );
    action->setData( static_cast< int >( mode ) );
    sutraMouseLookupGroup->addAction( action );
    sutraMouseLookupActions << action;

    connect( action, &QAction::triggered, this, [ this, mode ] {
      saveSutraMouseLookupMode( mode );
      showStatusBarMessage( tr( "Mouse lookup: %1" ).arg( sutraMouseLookupModeLabel( mode ) ), 5000 );
    } );

    return action;
  };

  addSutraMouseLookupAction( tr( "Disabled" ), SutraMouseLookupMode::Disabled );
  addSutraMouseLookupAction( tr( "Ctrl + Right Click" ), SutraMouseLookupMode::CtrlRightClick );
  addSutraMouseLookupAction( tr( "Ctrl + Left Click" ), SutraMouseLookupMode::CtrlLeftClick );
  addSutraMouseLookupAction( tr( "Alt + Right Click" ), SutraMouseLookupMode::AltRightClick );

  QAction * sutraMouseLookupInfoAction = sutraPopupLayoutMenu->addAction(
    tr( "Current mouse lookup: %1" ).arg( sutraMouseLookupModeLabel( loadSutraMouseLookupMode() ) ) );
  sutraMouseLookupInfoAction->setEnabled( false );

  sutraPopupLayoutMenu->addSeparator();

  QAction * sutraPopupRestoreDefaultsAction = sutraPopupLayoutMenu->addAction( tr( "Restore popup defaults" ) );

  const auto updateSutraPopupLayoutMenu = [ this,
                                            sutraPopupPinAction,
                                            sutraPopupAutoAction,
                                            sutraPopupFixedAction,
                                            sutraPopupFitAction,
                                            sutraPopupFontSizeActions,
                                            sutraPopupTransparencyActions,
                                            sutraMouseLookupActions,
                                            sutraMouseLookupInfoAction ] {
    sutraPopupPinAction->setChecked( ui.pinButton->isChecked() );

    const int currentFontSize = loadSutraPopupFontSize();
    for ( QAction * fontAction : sutraPopupFontSizeActions ) {
      fontAction->setChecked( fontAction->data().toInt() == currentFontSize );
    }

    const int currentOpacityPercent = loadSutraPopupOpacityPercent();
    for ( QAction * opacityAction : sutraPopupTransparencyActions ) {
      opacityAction->setChecked( opacityAction->data().toInt() == currentOpacityPercent );
    }

    const SutraMouseLookupMode currentMouseLookupMode = loadSutraMouseLookupMode();
    for ( QAction * mouseLookupAction : sutraMouseLookupActions ) {
      mouseLookupAction->setChecked( mouseLookupAction->data().toInt()
                                     == static_cast< int >( currentMouseLookupMode ) );
    }
    sutraMouseLookupInfoAction->setText(
      tr( "Current mouse lookup: %1" ).arg( sutraMouseLookupModeLabel( currentMouseLookupMode ) ) );

    switch ( loadSutraPopupLayoutMode() ) {
      case SutraPopupLayoutMode::Fixed:
        sutraPopupFixedAction->setChecked( true );
        break;
      case SutraPopupLayoutMode::FitToResults:
        sutraPopupFitAction->setChecked( true );
        break;
      case SutraPopupLayoutMode::Auto:
      default:
        sutraPopupAutoAction->setChecked( true );
        break;
    }
  };

  connect( sutraPopupLayoutMenu, &QMenu::aboutToShow, this, updateSutraPopupLayoutMenu );

  connect( sutraPopupPinAction, &QAction::triggered, this, [ this ]( bool checked ) {
    ui.pinButton->setChecked( checked );
    pinButtonClicked( checked );
    showStatusBarMessage( checked ? tr( "Popup pinned" ) : tr( "Popup unpinned" ), 4000 );
  } );

  connect( sutraPopupAutoAction, &QAction::triggered, this, [ this ] {
    saveSutraPopupLayoutMode( SutraPopupLayoutMode::Auto );
    showStatusBarMessage( tr( "Popup layout: Auto" ), 4000 );
  } );

  connect( sutraPopupFixedAction, &QAction::triggered, this, [ this ] {
    saveSutraPopupFixedGeometry( this );
    showStatusBarMessage( tr( "Popup layout: fixed current size and position" ), 5000 );
  } );

  connect( sutraPopupFitAction, &QAction::triggered, this, [ this ] {
    saveSutraPopupLayoutMode( SutraPopupLayoutMode::FitToResults );
    fitSutraPopupToResults( this, tabWidget );
    showStatusBarMessage( tr( "Popup layout: fit to results" ), 4000 );
  } );

  connect( sutraPopupRestoreDefaultsAction, &QAction::triggered, this, [ this ] {
    resetSutraPopupAppearanceDefaults();
    applySutraPopupOpacity( this );
    refreshSutraCustomTabs( tabWidget, pendingWord, translateBox->translateLine()->text() );
      applySutraPopupFontSizeToTabs( tabWidget );
    applySutraPopupLayoutMode( this, tabWidget );
    showStatusBarMessage( tr( "Popup defaults restored: Auto layout, 14 px, 100% opacity, Ctrl + Right Click" ), 5000 );
  } );

  if ( QToolButton * pinToolButton = qobject_cast< QToolButton * >( ui.pinButton ) ) {
    pinToolButton->setMenu( sutraPopupLayoutMenu );
    pinToolButton->setPopupMode( QToolButton::InstantPopup );
  }
  else {
    connect( ui.pinButton, &QAbstractButton::clicked, this, [ = ] {
      updateSutraPopupLayoutMenu();
      sutraPopupLayoutMenu->exec( ui.pinButton->mapToGlobal( QPoint( 0, ui.pinButton->height() ) ) );
    } );
  }

  QWidget * sutraPopupCornerTools = new QWidget( this );
  sutraPopupCornerTools->setObjectName( sutraPopupCornerToolsObjectName() );
  sutraPopupCornerTools->setAttribute( Qt::WA_TranslucentBackground );
  sutraPopupCornerTools->setToolTip( tr( "Popup quick settings" ) );
  sutraPopupCornerTools->setStyleSheet(
    QStringLiteral( "QWidget#sutraPopupCornerTools {"
                    "  background: rgba(245, 248, 252, 225);"
                    "  border: 1px solid rgba(120, 120, 120, 170);"
                    "  border-radius: 4px;"
                    "}"
                    "QToolButton {"
                    "  min-width: 22px;"
                    "  min-height: 20px;"
                    "  padding: 1px;"
                    "  border: 0;"
                    "  background: transparent;"
                    "  font-weight: bold;"
                    "}"
                    "QToolButton:hover {"
                    "  background: rgba(80, 140, 220, 60);"
                    "  border-radius: 3px;"
                    "}" ) );

  QHBoxLayout * sutraPopupCornerLayout = new QHBoxLayout( sutraPopupCornerTools );
  sutraPopupCornerLayout->setContentsMargins( 4, 2, 4, 2 );
  sutraPopupCornerLayout->setSpacing( 2 );

  QToolButton * sutraPopupOptionsButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupOptionsButton->setText( QStringLiteral( "⚙" ) );
  sutraPopupOptionsButton->setToolTip( tr( "Popup settings" ) );
  sutraPopupOptionsButton->setAutoRaise( true );

  QToolButton * sutraPopupQuickFixButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupQuickFixButton->setText( QStringLiteral( "📌" ) );
  sutraPopupQuickFixButton->setToolTip( tr( "Fix current size and position" ) );
  sutraPopupQuickFixButton->setAutoRaise( true );

  QToolButton * sutraPopupQuickFitButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupQuickFitButton->setText( QStringLiteral( "▣" ) );
  sutraPopupQuickFitButton->setToolTip( tr( "Fit window size to results" ) );
  sutraPopupQuickFitButton->setAutoRaise( true );

  sutraPopupCornerLayout->addWidget( sutraPopupOptionsButton );
  sutraPopupCornerLayout->addWidget( sutraPopupQuickFixButton );
  sutraPopupCornerLayout->addWidget( sutraPopupQuickFitButton );

  connect( sutraPopupOptionsButton, &QToolButton::clicked, this, [ = ] {
    updateSutraPopupLayoutMenu();
    positionSutraPopupCornerTools( this );
    sutraPopupLayoutMenu->exec(
      sutraPopupOptionsButton->mapToGlobal( QPoint( 0, sutraPopupOptionsButton->height() ) ) );
  } );

  connect( sutraPopupQuickFixButton, &QToolButton::clicked, this, [ = ] {
    sutraPopupFixedAction->trigger();
    positionSutraPopupCornerTools( this );
  } );

  connect( sutraPopupQuickFitButton, &QToolButton::clicked, this, [ = ] {
    sutraPopupFitAction->trigger();
    positionSutraPopupCornerTools( this );
  } );

  sutraPopupCornerTools->show();
  positionSutraPopupCornerTools( this );

  applySutraPopupLayoutMode( this, tabWidget );
  applySutraPopupOpacity( this );

#ifdef Q_OS_WIN
#include <windows.h>
#include <oleauto.h>
#include <QWidget>
  {
    QPointer< ScanPopup > popup( this );

    const bool mouseHookInstalled = registerSutraCtrlRightClickLookup( [ popup ]( const QPoint & globalPos ) {
      if ( !popup ) {
        return;
      }

      QClipboard * clipboard              = QApplication::clipboard();
      const QString previousClipboardText = clipboard ? clipboard->text( QClipboard::Clipboard ) : QString();
      const QString contextText           = sutraUiAutomationTextAtPoint( globalPos );

      releaseSutraControlKeys();
      releaseSutraAltKeys();
      sendSutraLeftDoubleClickAt( globalPos );

      QTimer::singleShot( 160, popup, [ popup, previousClipboardText, contextText ] {
        if ( !popup ) {
          return;
        }

        QClipboard * clipboard = QApplication::clipboard();
        if ( !clipboard ) {
          popup->showStatusBarMessage( QStringLiteral( "Clipboard is not available." ), 4000 );
          return;
        }

        clipboard->clear( QClipboard::Clipboard );
        sendSutraCtrlC();

        QTimer::singleShot( 220, popup, [ popup, previousClipboardText, contextText ] {
          if ( !popup ) {
            return;
          }

          QClipboard * clipboard = QApplication::clipboard();
          if ( !clipboard ) {
            return;
          }

          const QString capturedText = clipboard->text( QClipboard::Clipboard );
          const QString lookupText   = chooseCtrlRightClickLookupText( capturedText, contextText );

          if ( !lookupText.isEmpty() ) {
            popup->translateWord( lookupText );
            popup->showStatusBarMessage( QStringLiteral( "Ctrl + Right Click lookup: %1" ).arg( lookupText.left( 80 ) ),
                                         5000 );
          }
          else {
            popup->showStatusBarMessage(
              QStringLiteral( "No text captured. Try Ctrl + Right Click directly on selectable text." ),
              6000 );
          }

          clipboard->setText( previousClipboardText, QClipboard::Clipboard );
        } );
      } );
    } );

    if ( mouseHookInstalled ) {
      qInfo() << "Sutra mouse lookup hook installed";
    }
  }
#endif

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

  // Pin button left-click opens popup options menu. Pin/unpin is handled by the menu action above.

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
      view->setZoomFactor( cfg.preferences.zoomFactor * sutraPopupFontZoomFactor() );
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
  sutraForcePopupToFront( this );
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
  sutraForcePopupToFront( this );
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
      sutraForcePopupToFront( this );
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
      sutraForcePopupToFront( this );
    }
  }

  if ( ui.pinButton->isChecked() ) {
    setWindowTitle( QString( "%1 - GoldenDict-ng" ).arg( elideInputWord() ) );
  }

  /// Too large strings make window expand which is probably not what user
  /// wants
  translateBox->setText( Folding::escapeWildcardSymbols( pendingWord ), false );

  showTranslationFor( pendingWord );

  applySutraPopupLayoutMode( this, tabWidget );
  QTimer::singleShot( 250, this, [ this ] {
    applySutraPopupLayoutMode( this, tabWidget );
  } );
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

  if ( watched == this
       && ( event->type() == QEvent::Resize || event->type() == QEvent::Show
            || event->type() == QEvent::LayoutRequest ) ) {
    QTimer::singleShot( 0, this, [ this ] {
      positionSutraPopupCornerTools( this );
    } );
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

  if ( isVisible() && loadSutraPopupLayoutMode() == SutraPopupLayoutMode::Fixed ) {
    saveSutraPopupFixedGeometry( this );
  }

  QMainWindow::closeEvent( ev );
}

void ScanPopup::moveEvent( QMoveEvent * ev )
{
  if ( isVisible() && ui.pinButton->isChecked() ) {
    pinnedGeometry = saveGeometry();
  }

  if ( isVisible() && loadSutraPopupLayoutMode() == SutraPopupLayoutMode::Fixed ) {
    saveSutraPopupFixedGeometry( this );
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
