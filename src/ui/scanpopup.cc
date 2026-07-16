#include <QScreen>
#include <QStatusBar>

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
#include <QScrollBar>
#include <QTabWidget>
#include <QToolButton>
#include <QAbstractButton>
#include <QTabBar>
#include <QComboBox>
#include <QTimer>
#include <QTextDocument>
#include <QSettings>
#include <QApplication>
#include <QClipboard>
#include <QActionGroup>
#include <QPalette>
#include <QColor>
#include <QColorDialog>
#include <QPointer>
#include <QWebEnginePage>
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
#include <QFrame>
#include <QUrl>
#include <algorithm>
#include "scanpopup.hh"
#include "folding.hh"
#include "articlesaver.hh"
#include "utils.hh"
#include <QCursor>
#include <QPixmap>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <cmath>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QChildEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QVariantMap>
#include "gestures.hh"

using std::set;
using std::map;
using std::pair;

namespace {


void sutraApplyNativeTopmost( QWidget * window, bool enabled )
{
  if ( !window ) {
    return;
  }

#ifdef Q_OS_WIN
  HWND hwnd = reinterpret_cast< HWND >( window->winId() );
  if ( hwnd ) {
    SetWindowPos( hwnd,
                  enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
                  0,
                  0,
                  0,
                  0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW );
  }
#else
  Q_UNUSED( enabled );
#endif
}

void sutraForcePopupToFront( QWidget * window )
{
  if ( !window ) {
    return;
  }

  const bool keepTopmost = window->windowFlags().testFlag( Qt::WindowStaysOnTopHint );

  window->show();
  window->raise();
  window->activateWindow();

#ifdef Q_OS_WIN
  HWND hwnd = reinterpret_cast< HWND >( window->winId() );

  if ( hwnd ) {
    // Bring the popup forward without accidentally clearing a persistent
    // Always-on-top setting. The old code always called HWND_NOTOPMOST here,
    // which made the checkbox appear enabled while Windows had already removed it.
    SetWindowPos( hwnd,
                  HWND_TOPMOST,
                  0,
                  0,
                  0,
                  0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW );
    SetForegroundWindow( hwnd );
    if ( !keepTopmost ) {
      SetWindowPos( hwnd,
                    HWND_NOTOPMOST,
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW );
    }
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


QString sutraPopupLayoutModeSettingsKeyFocusBaseV2()
;

QString sutraPopupLayoutModeSettingsKey()
{
  QString base = sutraPopupLayoutModeSettingsKeyFocusBaseV2(  );

  // sutraGlossaryUiPolishV4
  // Keep the UI mostly inline because QTextBrowser supports a limited CSS subset on Windows.

  base.replace( QStringLiteral( "NGH&#296;A TI&#7870;NG VI&#7878;T" ),
                QStringLiteral( "<div style='margin:16px 0 8px 0;font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>NGH&#296;A TI&#7870;NG VI&#7878;T</div>" ) );
  base.replace( QStringLiteral( "NGHÄ¨A TIáº¾NG VIá»†T" ),
                QStringLiteral( "<div style='margin:16px 0 8px 0;font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>NGH&#296;A TI&#7870;NG VI&#7878;T</div>" ) );

  base.replace( QStringLiteral( "PINYIN" ),
                QStringLiteral( "<div style='margin:12px 0 5px 0;font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>PINYIN</div>" ) );

  base.replace( QStringLiteral( "G&#7906;I &#221; D&#7882;CH" ),
                QStringLiteral( "<div style='margin:12px 0 6px 0;font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>G&#7906;I &#221; D&#7882;CH</div>" ) );
  base.replace( QStringLiteral( "Gá»¢I Ã Dá»ŠCH" ),
                QStringLiteral( "<div style='margin:12px 0 6px 0;font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>G&#7906;I &#221; D&#7882;CH</div>" ) );

  base.replace( QStringLiteral( "LI&#202;N QUAN" ),
                QStringLiteral( "<div style='margin:12px 0 6px 0;font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>LI&#202;N QUAN</div>" ) );
  base.replace( QStringLiteral( "LIÃŠN QUAN" ),
                QStringLiteral( "<div style='margin:12px 0 6px 0;font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>LI&#202;N QUAN</div>" ) );

  base.replace( QStringLiteral( "class='chip'" ),
                QStringLiteral( "style='display:inline-block;margin:4px 7px 4px 0;padding:4px 9px;border-radius:12px;background:#f8fafc;border:1px solid #e2e8f0;color:#334155;font-weight:700;'" ) );
  base.replace( QStringLiteral( "class=\"chip\"" ),
                QStringLiteral( "style=\"display:inline-block;margin:4px 7px 4px 0;padding:4px 9px;border-radius:12px;background:#f8fafc;border:1px solid #e2e8f0;color:#334155;font-weight:700;\"" ) );

  base.replace( QStringLiteral( "class='badge'" ),
                QStringLiteral( "style='display:inline-block;margin:6px 0 12px 0;padding:5px 9px;border-radius:10px;background:#ecfdf5;border:1px solid #bbf7d0;color:#047857;font-weight:800;'" ) );
  base.replace( QStringLiteral( "class=\"badge\"" ),
                QStringLiteral( "style=\"display:inline-block;margin:6px 0 12px 0;padding:5px 9px;border-radius:10px;background:#ecfdf5;border:1px solid #bbf7d0;color:#047857;font-weight:800;\"" ) );

  base.replace( QStringLiteral( "<a " ),
                QStringLiteral( "<a style='display:inline-block;margin:4px 7px 4px 0;padding:4px 9px;border-radius:12px;background:#fff7ed;border:1px solid #fed7aa;color:#9a3412;text-decoration:none;font-weight:700;' " ) );

  const QString primaryOpen = QStringLiteral(
    "<div style='box-sizing:border-box;margin:10px 8px 18px 0;padding:22px 26px 24px 26px;"
    "background:#ffffff;border:1px solid #dbeafe;border-left:6px solid #0f766e;'>"
    "<div style='font-size:12px;font-weight:800;letter-spacing:1px;color:#0f766e;margin:0 0 12px 0;'>"
    "K&#7870;T QU&#7842; CH&#205;NH</div>" );

  const QString relatedOpen = QStringLiteral(
    "<div style='box-sizing:border-box;margin:8px 8px 12px 0;padding:14px 16px;"
    "background:#fffdf8;border:1px solid #fed7aa;border-left:4px solid #f97316;'>" );

  const QString close = QStringLiteral( "</div>" );

  if ( true ) {
    base.replace( QStringLiteral( "<h1" ), QStringLiteral( "<h1 style='font-size:34px;line-height:1.18;margin:0 0 8px 0;color:#020617;font-weight:800;'" ) );
    base.replace( QStringLiteral( "<h2" ), QStringLiteral( "<h2 style='font-size:34px;line-height:1.18;margin:0 0 8px 0;color:#020617;font-weight:800;'" ) );
    base.replace( QStringLiteral( "<h3" ), QStringLiteral( "<h3 style='font-size:34px;line-height:1.18;margin:0 0 8px 0;color:#020617;font-weight:800;'" ) );

    return primaryOpen + base + close;
  }

  base.replace( QStringLiteral( "<h1" ), QStringLiteral( "<h1 style='font-size:23px;line-height:1.2;margin:0 0 4px 0;color:#7c2d12;font-weight:750;'" ) );
  base.replace( QStringLiteral( "<h2" ), QStringLiteral( "<h2 style='font-size:23px;line-height:1.2;margin:0 0 4px 0;color:#7c2d12;font-weight:750;'" ) );
  base.replace( QStringLiteral( "<h3" ), QStringLiteral( "<h3 style='font-size:23px;line-height:1.2;margin:0 0 4px 0;color:#7c2d12;font-weight:750;'" ) );

  return relatedOpen + base + close;
}

QString sutraPopupLayoutModeSettingsKeyFocusBaseV2()
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

QString sutraPopupZoomIndicatorObjectName()
{
  return QStringLiteral( "sutraPopupZoomIndicator" );
}

int sutraPopupZoomPercent()
{
  return qRound( 100.0 * static_cast< double >( loadSutraPopupFontSize() )
                 / static_cast< double >( sutraPopupDefaultFontSize ) );
}

int nextSutraPopupFontSize( int direction )
{
  const QList< int > levels = { 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 32, 36 };
  const int current          = loadSutraPopupFontSize();

  if ( direction > 0 ) {
    for ( int level : levels ) {
      if ( level > current ) {
        return level;
      }
    }
    return levels.last();
  }

  if ( direction < 0 ) {
    for ( auto it = levels.crbegin(); it != levels.crend(); ++it ) {
      if ( *it < current ) {
        return *it;
      }
    }
    return levels.first();
  }

  return sutraPopupDefaultFontSize;
}

void updateSutraPopupZoomIndicator( QWidget * popup )
{
  if ( !popup ) {
    return;
  }

  if ( QToolButton * indicator =
         popup->findChild< QToolButton * >( sutraPopupZoomIndicatorObjectName() ) ) {
    indicator->setText( QStringLiteral( "%1%" ).arg( sutraPopupZoomPercent() ) );
  }
}


bool sutraPopupUsesDarkTheme();

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

  const QString browserTheme = sutraPopupUsesDarkTheme()
                                 ? QStringLiteral( "background:#0b1220;color:#e5e7eb;" )
                                 : QStringLiteral( "background:#ffffff;color:#0f172a;" );

  browser->setStyleSheet(
    QStringLiteral( "QTextBrowser { font-size: %1px; %2 }" ).arg( fontSize ).arg( browserTheme ) );
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


enum class SutraPopupThemeMode {
  Light  = 0,
  Dark   = 1,
  Custom = 2
};

QString sutraPopupThemeSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupThemeMode" );
}

QString sutraPopupShowGlossaryTabSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupShowGlossaryTab" );
}

QString sutraPopupShowWebTabSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupShowWebTab" );
}

constexpr bool sutraPopupDefaultShowGlossaryTab = false;
constexpr bool sutraPopupDefaultShowWebTab      = false;

bool loadSutraPopupShowGlossaryTab()
{
  QSettings settings;
  return settings.value( sutraPopupShowGlossaryTabSettingsKey(),
                         sutraPopupDefaultShowGlossaryTab ).toBool();
}

bool loadSutraPopupShowWebTab()
{
  QSettings settings;
  return settings.value( sutraPopupShowWebTabSettingsKey(),
                         sutraPopupDefaultShowWebTab ).toBool();
}

void saveSutraPopupShowGlossaryTab( bool visible )
{
  QSettings settings;
  settings.setValue( sutraPopupShowGlossaryTabSettingsKey(), visible );
}

void saveSutraPopupShowWebTab( bool visible )
{
  QSettings settings;
  settings.setValue( sutraPopupShowWebTabSettingsKey(), visible );
}

QString sutraGlossaryTabTitle()
{
  // UTF-8 byte escapes keep non-ASCII UI text independent of source-file encoding.
  return QString::fromUtf8( "\x47" "\x69" "\xe1" "\xba" "\xa3" "\x69" "\x20"
                            "\x6e" "\x67" "\x68" "\xc4" "\xa9" "\x61" );
}

QString sutraPopupThemeToggleButtonObjectName()
{
  return QStringLiteral( "sutraPopupThemeToggleButton" );
}

QString sutraPopupColorsButtonObjectName()
{
  return QStringLiteral( "sutraPopupColorsButton" );
}

QString sutraPopupCustomTextColorSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupCustomTextColor" );
}

QString sutraPopupCustomBackgroundColorSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupCustomBackgroundColor" );
}

QColor sutraPopupDefaultCustomTextColor()
{
  return QColor( 17, 17, 17 );
}

QColor sutraPopupDefaultCustomBackgroundColor()
{
  return QColor( 255, 255, 255 );
}

QColor loadSutraPopupCustomTextColor()
{
  QSettings settings;
  const QColor color( settings.value( sutraPopupCustomTextColorSettingsKey(),
                                      sutraPopupDefaultCustomTextColor().name() ).toString() );
  return color.isValid() ? color : sutraPopupDefaultCustomTextColor();
}

QColor loadSutraPopupCustomBackgroundColor()
{
  QSettings settings;
  const QColor color( settings.value( sutraPopupCustomBackgroundColorSettingsKey(),
                                      sutraPopupDefaultCustomBackgroundColor().name() ).toString() );
  return color.isValid() ? color : sutraPopupDefaultCustomBackgroundColor();
}

void saveSutraPopupCustomTextColor( const QColor & color )
{
  if ( !color.isValid() ) {
    return;
  }

  QSettings settings;
  settings.setValue( sutraPopupCustomTextColorSettingsKey(), color.name( QColor::HexRgb ) );
}

void saveSutraPopupCustomBackgroundColor( const QColor & color )
{
  if ( !color.isValid() ) {
    return;
  }

  QSettings settings;
  settings.setValue( sutraPopupCustomBackgroundColorSettingsKey(), color.name( QColor::HexRgb ) );
}

constexpr int sutraPopupDefaultThemeMode = static_cast< int >( SutraPopupThemeMode::Light );

SutraPopupThemeMode loadSutraPopupThemeMode()
{
  QSettings settings;
  const int value = qBound( 0,
                            settings.value( sutraPopupThemeSettingsKey(), sutraPopupDefaultThemeMode ).toInt(),
                            2 );
  return static_cast< SutraPopupThemeMode >( value );
}

void saveSutraPopupThemeMode( SutraPopupThemeMode mode )
{
  QSettings settings;
  settings.setValue( sutraPopupThemeSettingsKey(), static_cast< int >( mode ) );
}

bool sutraPopupUsesDarkTheme()
{
  return loadSutraPopupThemeMode() == SutraPopupThemeMode::Dark;
}

bool sutraPopupUsesCustomTheme()
{
  return loadSutraPopupThemeMode() == SutraPopupThemeMode::Custom;
}

QColor sutraPopupActiveTextColor()
{
  switch ( loadSutraPopupThemeMode() ) {
    case SutraPopupThemeMode::Dark:
      return QColor( 255, 255, 255 );
    case SutraPopupThemeMode::Custom:
      return loadSutraPopupCustomTextColor();
    case SutraPopupThemeMode::Light:
    default:
      return QColor( 0, 0, 0 );
  }
}

QColor sutraPopupActiveBackgroundColor()
{
  switch ( loadSutraPopupThemeMode() ) {
    case SutraPopupThemeMode::Dark:
      return QColor( 0, 0, 0 );
    case SutraPopupThemeMode::Custom:
      return loadSutraPopupCustomBackgroundColor();
    case SutraPopupThemeMode::Light:
    default:
      return QColor( 255, 255, 255 );
  }
}

QColor sutraPopupRaisedSurfaceColor( const QColor & background )
{
  return background.lightnessF() < 0.5 ? background.lighter( 145 ) : background.darker( 106 );
}

QColor sutraPopupBorderColor( const QColor & background, const QColor & text )
{
  QColor mixed;
  mixed.setRed( ( background.red() * 2 + text.red() ) / 3 );
  mixed.setGreen( ( background.green() * 2 + text.green() ) / 3 );
  mixed.setBlue( ( background.blue() * 2 + text.blue() ) / 3 );
  return mixed;
}

QIcon sutraPopupColorSwatchIcon( const QColor & color )
{
  QPixmap pixmap( 16, 16 );
  pixmap.fill( color );
  return QIcon( pixmap );
}

QIcon sutraPopupThemeToggleIcon( SutraPopupThemeMode mode, const QColor & color )
{
  QPixmap pixmap( 20, 20 );
  pixmap.fill( Qt::transparent );

  QPainter painter( &pixmap );
  painter.setRenderHint( QPainter::Antialiasing, true );
  QPen pen( color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
  painter.setPen( pen );
  painter.setBrush( color );

  if ( mode == SutraPopupThemeMode::Dark ) {
    // Current mode is Dark: show a sun icon because clicking switches to Light.
    painter.setBrush( Qt::NoBrush );
    painter.drawEllipse( QRectF( 7.0, 7.0, 6.0, 6.0 ) );
    const QPointF center( 10.0, 10.0 );
    for ( int i = 0; i < 8; ++i ) {
      const qreal angle = qDegreesToRadians( static_cast< qreal >( i * 45 ) );
      const QPointF inner( center.x() + std::cos( angle ) * 5.0,
                           center.y() + std::sin( angle ) * 5.0 );
      const QPointF outer( center.x() + std::cos( angle ) * 7.2,
                           center.y() + std::sin( angle ) * 7.2 );
      painter.drawLine( inner, outer );
    }
  }
  else if ( mode == SutraPopupThemeMode::Light ) {
    // Current mode is Light: show a moon icon because clicking switches to Dark.
    QPainterPath outer;
    outer.addEllipse( QRectF( 4.0, 3.0, 12.0, 14.0 ) );
    QPainterPath cutout;
    cutout.addEllipse( QRectF( 8.0, 1.5, 12.0, 14.0 ) );
    painter.fillPath( outer.subtracted( cutout ), color );
  }
  else {
    // Custom colors: use a small palette-style icon.
    painter.setBrush( Qt::NoBrush );
    painter.drawEllipse( QRectF( 3.0, 3.0, 14.0, 14.0 ) );
    painter.drawEllipse( QRectF( 6.0, 6.0, 1.8, 1.8 ) );
    painter.drawEllipse( QRectF( 10.0, 5.0, 1.8, 1.8 ) );
    painter.drawEllipse( QRectF( 12.5, 8.5, 1.8, 1.8 ) );
    painter.drawLine( QPointF( 7.0, 14.0 ), QPointF( 13.5, 14.0 ) );
  }

  return QIcon( pixmap );
}


QColor sutraPopupToolbarIconColor( bool enabled )
{
  if ( enabled ) {
    return sutraPopupActiveTextColor();
  }
  return sutraPopupBorderColor( sutraPopupActiveBackgroundColor(), sutraPopupActiveTextColor() );
}

QIcon sutraPopupNavigationIcon( bool forward, const QColor & color )
{
  QPixmap pixmap( 18, 18 );
  pixmap.fill( Qt::transparent );

  QPainter painter( &pixmap );
  painter.setRenderHint( QPainter::Antialiasing, true );
  painter.setPen( QPen( color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );

  const qreal direction = forward ? 1.0 : -1.0;
  const QPointF tip( 9.0 + direction * 4.0, 9.0 );
  painter.drawLine( QPointF( 9.0 - direction * 4.0, 9.0 ), tip );
  painter.drawLine( tip, QPointF( 9.0 + direction * 0.5, 4.8 ) );
  painter.drawLine( tip, QPointF( 9.0 + direction * 0.5, 13.2 ) );
  return QIcon( pixmap );
}

QIcon sutraPopupHistoryIcon( const QColor & color )
{
  QPixmap pixmap( 18, 18 );
  pixmap.fill( Qt::transparent );

  QPainter painter( &pixmap );
  painter.setRenderHint( QPainter::Antialiasing, true );
  painter.setPen( QPen( color, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
  painter.setBrush( Qt::NoBrush );
  painter.drawEllipse( QRectF( 3.0, 3.0, 12.0, 12.0 ) );
  painter.drawLine( QPointF( 9.0, 5.7 ), QPointF( 9.0, 9.2 ) );
  painter.drawLine( QPointF( 9.0, 9.2 ), QPointF( 11.7, 10.8 ) );
  return QIcon( pixmap );
}

QIcon sutraPopupNoteIcon( const QColor & color, bool hasNote )
{
  QPixmap pixmap( 18, 18 );
  pixmap.fill( Qt::transparent );

  QPainter painter( &pixmap );
  painter.setRenderHint( QPainter::Antialiasing, true );
  painter.setPen( QPen( color, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
  painter.setBrush( Qt::NoBrush );

  QPainterPath page;
  page.moveTo( 4.0, 2.8 );
  page.lineTo( 11.5, 2.8 );
  page.lineTo( 14.0, 5.4 );
  page.lineTo( 14.0, 15.0 );
  page.lineTo( 4.0, 15.0 );
  page.closeSubpath();
  painter.drawPath( page );
  painter.drawLine( QPointF( 11.5, 2.8 ), QPointF( 11.5, 5.5 ) );
  painter.drawLine( QPointF( 11.5, 5.5 ), QPointF( 14.0, 5.5 ) );
  painter.drawLine( QPointF( 6.2, 8.1 ), QPointF( 11.8, 8.1 ) );
  painter.drawLine( QPointF( 6.2, 10.6 ), QPointF( 11.8, 10.6 ) );

  if ( hasNote ) {
    painter.setPen( Qt::NoPen );
    painter.setBrush( color );
    painter.drawEllipse( QRectF( 12.3, 12.3, 4.2, 4.2 ) );
  }
  return QIcon( pixmap );
}


QIcon sutraPopupSettingsIcon( const QColor & color )
{
  QPixmap pixmap( 20, 20 );
  pixmap.fill( Qt::transparent );

  QPainter painter( &pixmap );
  painter.setRenderHint( QPainter::Antialiasing, true );
  painter.setPen( QPen( color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
  painter.setBrush( Qt::NoBrush );

  const QPointF center( 10.0, 10.0 );
  painter.drawEllipse( QRectF( 7.0, 7.0, 6.0, 6.0 ) );
  painter.drawEllipse( QRectF( 4.3, 4.3, 11.4, 11.4 ) );
  for ( int i = 0; i < 8; ++i ) {
    const qreal angle = qDegreesToRadians( static_cast< qreal >( i * 45 ) );
    const QPointF inner( center.x() + std::cos( angle ) * 6.0,
                         center.y() + std::sin( angle ) * 6.0 );
    const QPointF outer( center.x() + std::cos( angle ) * 8.0,
                         center.y() + std::sin( angle ) * 8.0 );
    painter.drawLine( inner, outer );
  }
  return QIcon( pixmap );
}

QIcon sutraPopupBackToTopIcon( const QColor & color )
{
  QPixmap pixmap( 18, 18 );
  pixmap.fill( Qt::transparent );

  QPainter painter( &pixmap );
  painter.setRenderHint( QPainter::Antialiasing, true );
  painter.setPen( QPen( color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
  painter.drawLine( QPointF( 4.0, 3.5 ), QPointF( 14.0, 3.5 ) );
  painter.drawLine( QPointF( 9.0, 14.5 ), QPointF( 9.0, 6.0 ) );
  painter.drawLine( QPointF( 9.0, 6.0 ), QPointF( 5.5, 9.5 ) );
  painter.drawLine( QPointF( 9.0, 6.0 ), QPointF( 12.5, 9.5 ) );
  return QIcon( pixmap );
}

QIcon sutraPopupFixedLayoutIcon( const QColor & color )
{
  QPixmap pixmap( 18, 18 );
  pixmap.fill( Qt::transparent );

  QPainter painter( &pixmap );
  painter.setRenderHint( QPainter::Antialiasing, true );
  painter.setPen( QPen( color, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
  painter.setBrush( Qt::NoBrush );
  painter.drawRoundedRect( QRectF( 2.8, 3.2, 12.4, 10.8 ), 1.5, 1.5 );
  painter.drawLine( QPointF( 5.0, 6.0 ), QPointF( 13.0, 6.0 ) );
  painter.drawLine( QPointF( 11.0, 11.0 ), QPointF( 14.8, 14.8 ) );
  painter.drawLine( QPointF( 14.8, 14.8 ), QPointF( 14.8, 11.8 ) );
  painter.drawLine( QPointF( 14.8, 14.8 ), QPointF( 11.8, 14.8 ) );
  return QIcon( pixmap );
}

QString sutraPopupThemeModeLabel( SutraPopupThemeMode mode )
{
  switch ( mode ) {
    case SutraPopupThemeMode::Dark:
      return QStringLiteral( "Dark" );
    case SutraPopupThemeMode::Custom:
      return QStringLiteral( "Custom" );
    case SutraPopupThemeMode::Light:
    default:
      return QStringLiteral( "Light" );
  }
}

QString sutraApplyPopupThemeToHtml( QString html )
{
  if ( html.isEmpty() ) {
    return html;
  }

  if ( sutraPopupUsesCustomTheme() ) {
    const QString textColor = loadSutraPopupCustomTextColor().name( QColor::HexRgb );
    const QString backgroundColor = loadSutraPopupCustomBackgroundColor().name( QColor::HexRgb );
    const QString customStyle = QStringLiteral(
      "<style id='sutra-popup-custom-colors'>"
      "html,body{background:%1!important;color:%2!important;}"
      "body{padding-bottom:52px!important;box-sizing:border-box!important;}"
      "body *{color:%2!important;border-color:%2!important;}"
      "body div,body section,body article,body header,body footer,body main,body aside,"
      "body p,body span,body h1,body h2,body h3,body h4,body h5,body h6,body ul,body ol,body li,"
      "body table,body tr,body td,body th,body blockquote,body pre,body code{background:transparent!important;}"
      "body a,body a *{color:%2!important;}"
      "body input,body textarea,body select,body button{background:%1!important;color:%2!important;border-color:%2!important;}"
      "</style>" ).arg( backgroundColor, textColor );

    const qsizetype headEnd = html.indexOf( QStringLiteral( "</head>" ), 0, Qt::CaseInsensitive );
    if ( headEnd >= 0 ) {
      html.insert( headEnd, customStyle );
    }
    else {
      html.prepend( customStyle );
    }
    return html;
  }

  if ( !sutraPopupUsesDarkTheme() ) {
    return html;
  }

  struct ThemeColorReplacement {
    const char * lightColor;
    const char * darkColor;
  };

  // Use temporary tokens first so replacements cannot cascade into one another.
  const ThemeColorReplacement replacements[] = {
    { "#ffffff", "#111827" }, { "#f9fafb", "#0b1220" }, { "#f8fafc", "#0b1220" },
    { "#f7f9fc", "#0b1220" }, { "#f6f8fb", "#0b1220" }, { "#f1f5f9", "#1e293b" },
    { "#ecfdf5", "#063c35" }, { "#eff6ff", "#172554" },
    { "#fff7ed", "#431407" }, { "#fffdf8", "#2b1609" }, { "#f0fdf4", "#052e16" },
    { "#fff", "#111827" },
    { "#e2e8f0", "#334155" }, { "#cbd5e1", "#475569" }, { "#dbeafe", "#1e3a5f" },
    { "#bfdbfe", "#1e40af" }, { "#bbf7d0", "#166534" }, { "#fed7aa", "#9a3412" },
    { "#fdba74", "#c2410c" }, { "#0f172a", "#e5e7eb" }, { "#020617", "#f8fafc" },
    { "#1f2937", "#e5e7eb" }, { "#111827", "#f1f5f9" }, { "#334155", "#cbd5e1" }, { "#475569", "#cbd5e1" },
    { "#64748b", "#94a3b8" }, { "#94a3b8", "#64748b" }, { "#0f766e", "#5eead4" },
    { "#047857", "#6ee7b7" }, { "#1d4ed8", "#93c5fd" }, { "#2563eb", "#60a5fa" },
    { "#3b82f6", "#60a5fa" }, { "#7c2d12", "#fdba74" }, { "#9a3412", "#fdba74" },
    { "#c2410c", "#fb923c" }, { "#f97316", "#fb923c" }
  };

  for ( qsizetype i = 0; i < static_cast< qsizetype >( sizeof( replacements ) / sizeof( replacements[ 0 ] ) ); ++i ) {
    html.replace( QString::fromLatin1( replacements[ i ].lightColor ),
                  QStringLiteral( "__SUTRA_THEME_COLOR_%1__" ).arg( i ),
                  Qt::CaseInsensitive );
  }

  for ( qsizetype i = 0; i < static_cast< qsizetype >( sizeof( replacements ) / sizeof( replacements[ 0 ] ) ); ++i ) {
    html.replace( QStringLiteral( "__SUTRA_THEME_COLOR_%1__" ).arg( i ),
                  QString::fromLatin1( replacements[ i ].darkColor ) );
  }

  return html;
}

QString sutraPopupArticleThemeScript( bool dark, bool monochrome )
{
  if ( monochrome ) {
    const QColor background = sutraPopupActiveBackgroundColor();
    const QColor text = sutraPopupActiveTextColor();
    const QString colorScheme = background.lightnessF() < 0.5 ? QStringLiteral( "dark" )
                                                                : QStringLiteral( "light" );

    return QStringLiteral( R"JS(
      (function () {
        const root = document.documentElement;
        if (!root) return;

        const styleId = 'sutra-popup-content-theme';
        const themeGeneration = (window.__sutraPopupThemeGeneration || 0) + 1;
        window.__sutraPopupThemeGeneration = themeGeneration;

        function applyMonochromeTheme() {
          if (window.__sutraPopupThemeGeneration !== themeGeneration) return;

          try {
            if (window.DarkReader && typeof window.DarkReader.disable === 'function') {
              window.DarkReader.disable();
            }
          } catch (_) {
          }

          window.gdDarkModeInjected = false;
          root.removeAttribute('data-darkreader-mode');
          root.removeAttribute('data-darkreader-scheme');
          root.classList.remove('sutra-popup-force-invert');

          document.querySelectorAll(
            'style.darkreader, link.darkreader, '
            + 'style[class^="darkreader--"], style[class*=" darkreader--"], '
            + 'link[class^="darkreader--"], link[class*=" darkreader--"], '
            + 'meta[name="darkreader-lock"]'
          ).forEach(function (node) {
            node.remove();
          });

          let style = document.getElementById(styleId);
          if (!style) {
            style = document.createElement('style');
            style.id = styleId;
            (document.head || root).appendChild(style);
          }

          style.textContent = `
            html[data-sutra-popup-theme="monochrome"] {
              color-scheme: %3 !important;
              background-color: %1 !important;
              color: %2 !important;
              filter: none !important;
            }
            html[data-sutra-popup-theme="monochrome"] body {
              min-height: 100vh !important;
              padding-bottom: 52px !important;
              box-sizing: border-box !important;
              background-color: %1 !important;
              color: %2 !important;
              filter: none !important;
            }
            html[data-sutra-popup-theme="monochrome"] body * {
              color: %2 !important;
              background-color: transparent !important;
              background-image: none !important;
              text-shadow: none !important;
              border-color: %2 !important;
            }
            html[data-sutra-popup-theme="monochrome"] body div,
            html[data-sutra-popup-theme="monochrome"] body section,
            html[data-sutra-popup-theme="monochrome"] body article,
            html[data-sutra-popup-theme="monochrome"] body header,
            html[data-sutra-popup-theme="monochrome"] body footer,
            html[data-sutra-popup-theme="monochrome"] body main,
            html[data-sutra-popup-theme="monochrome"] body aside,
            html[data-sutra-popup-theme="monochrome"] body p,
            html[data-sutra-popup-theme="monochrome"] body span,
            html[data-sutra-popup-theme="monochrome"] body h1,
            html[data-sutra-popup-theme="monochrome"] body h2,
            html[data-sutra-popup-theme="monochrome"] body h3,
            html[data-sutra-popup-theme="monochrome"] body h4,
            html[data-sutra-popup-theme="monochrome"] body h5,
            html[data-sutra-popup-theme="monochrome"] body h6,
            html[data-sutra-popup-theme="monochrome"] body ul,
            html[data-sutra-popup-theme="monochrome"] body ol,
            html[data-sutra-popup-theme="monochrome"] body li,
            html[data-sutra-popup-theme="monochrome"] body table,
            html[data-sutra-popup-theme="monochrome"] body tr,
            html[data-sutra-popup-theme="monochrome"] body td,
            html[data-sutra-popup-theme="monochrome"] body th,
            html[data-sutra-popup-theme="monochrome"] body blockquote,
            html[data-sutra-popup-theme="monochrome"] body pre,
            html[data-sutra-popup-theme="monochrome"] body code {
              background-color: transparent !important;
              background-image: none !important;
            }
            html[data-sutra-popup-theme="monochrome"] body a,
            html[data-sutra-popup-theme="monochrome"] body a * {
              color: %2 !important;
            }
            html[data-sutra-popup-theme="monochrome"] body input,
            html[data-sutra-popup-theme="monochrome"] body textarea,
            html[data-sutra-popup-theme="monochrome"] body select,
            html[data-sutra-popup-theme="monochrome"] body button {
              background-color: %1 !important;
              color: %2 !important;
              border-color: %2 !important;
            }
            html[data-sutra-popup-theme="monochrome"] img,
            html[data-sutra-popup-theme="monochrome"] picture,
            html[data-sutra-popup-theme="monochrome"] video,
            html[data-sutra-popup-theme="monochrome"] canvas,
            html[data-sutra-popup-theme="monochrome"] svg,
            html[data-sutra-popup-theme="monochrome"] iframe {
              filter: none !important;
            }
            html[data-sutra-popup-theme="monochrome"] ::selection {
              background-color: %2 !important;
              color: %1 !important;
            }
          `;

          root.setAttribute('data-sutra-popup-theme', 'monochrome');
        }

        applyMonochromeTheme();
        [0, 40, 120, 300, 700, 1500].forEach(function (delay) {
          window.setTimeout(applyMonochromeTheme, delay);
        });
      })();
    )JS" ).arg( background.name( QColor::HexRgb ), text.name( QColor::HexRgb ), colorScheme );
  }

  if ( !dark ) {
    return QStringLiteral( R"JS(
      (function () {
        const root = document.documentElement;
        if (!root) return;

        const styleId = 'sutra-popup-content-theme';
        const themeGeneration = (window.__sutraPopupThemeGeneration || 0) + 1;
        window.__sutraPopupThemeGeneration = themeGeneration;

        function removeDarkReaderArtifacts() {
          if (window.__sutraPopupThemeGeneration !== themeGeneration) return;
          // GoldenDict-ng may generate the first dictionary tab with its global
          // Dark Reader preference. Popup Light mode must remain independent.
          try {
            if (window.DarkReader && typeof window.DarkReader.disable === 'function') {
              window.DarkReader.disable();
            }
          } catch (_) {
          }

          // Allow a future website navigation to initialise Dark Reader again
          // when it is actually requested outside this popup Light mode.
          window.gdDarkModeInjected = false;

          root.removeAttribute('data-darkreader-mode');
          root.removeAttribute('data-darkreader-scheme');
          root.removeAttribute('data-sutra-popup-theme');
          root.classList.remove('sutra-popup-force-invert');

          document.querySelectorAll(
            'style.darkreader, link.darkreader, '
            + 'style[class^="darkreader--"], style[class*=" darkreader--"], '
            + 'link[class^="darkreader--"], link[class*=" darkreader--"], '
            + 'meta[name="darkreader-lock"]'
          ).forEach(function (node) {
            node.remove();
          });

          let style = document.getElementById(styleId);
          if (!style) {
            style = document.createElement('style');
            style.id = styleId;
            (document.head || root).appendChild(style);
          }

          style.textContent = `
            html[data-sutra-popup-theme="light"] {
              color-scheme: light !important;
              background-color: #ffffff !important;
              filter: none !important;
            }
            html[data-sutra-popup-theme="light"] body {
              min-height: 100vh !important;
              padding-bottom: 52px !important;
              box-sizing: border-box !important;
              background-color: #ffffff !important;
              color: #111827 !important;
              filter: none !important;
            }
            html[data-sutra-popup-theme="light"] img,
            html[data-sutra-popup-theme="light"] picture,
            html[data-sutra-popup-theme="light"] video,
            html[data-sutra-popup-theme="light"] canvas,
            html[data-sutra-popup-theme="light"] svg,
            html[data-sutra-popup-theme="light"] iframe {
              filter: none !important;
            }
          `;

          root.setAttribute('data-sutra-popup-theme', 'light');
        }

        removeDarkReaderArtifacts();

        // Dark Reader can finish asynchronously after loadFinished. Repeat the
        // cleanup briefly so the first dictionary tab cannot turn dark again.
        [0, 40, 120, 300, 700, 1500].forEach(function (delay) {
          window.setTimeout(removeDarkReaderArtifacts, delay);
        });
      })();
    )JS" );
  }

  return QStringLiteral( R"JS(
    (function () {
      const root = document.documentElement;
      if (!root) return;

      window.__sutraPopupThemeGeneration = (window.__sutraPopupThemeGeneration || 0) + 1;

      const styleId = 'sutra-popup-content-theme';
      let style = document.getElementById(styleId);
      if (!style) {
        style = document.createElement('style');
        style.id = styleId;
        (document.head || root).appendChild(style);
      }

      style.textContent = `
        html[data-sutra-popup-theme="dark"] {
          color-scheme: dark !important;
          background-color: #0b1220 !important;
        }
        html[data-sutra-popup-theme="dark"] body {
          min-height: 100vh !important;
          padding-bottom: 52px !important;
          box-sizing: border-box !important;
          background-color: #0b1220 !important;
          color: #e5e7eb !important;
        }
        html[data-sutra-popup-theme="dark"] a {
          color: #60a5fa !important;
        }
        html[data-sutra-popup-theme="dark"] input,
        html[data-sutra-popup-theme="dark"] textarea,
        html[data-sutra-popup-theme="dark"] select,
        html[data-sutra-popup-theme="dark"] button {
          background-color: #111827 !important;
          color: #e5e7eb !important;
          border-color: #475569 !important;
        }
        html[data-sutra-popup-theme="dark"] pre,
        html[data-sutra-popup-theme="dark"] code,
        html[data-sutra-popup-theme="dark"] blockquote,
        html[data-sutra-popup-theme="dark"] table,
        html[data-sutra-popup-theme="dark"] th,
        html[data-sutra-popup-theme="dark"] td {
          border-color: #334155 !important;
        }
        html[data-sutra-popup-theme="dark"] ::selection {
          background-color: #2563eb !important;
          color: #ffffff !important;
        }
        html[data-sutra-popup-theme="dark"].sutra-popup-force-invert {
          filter: invert(0.90) hue-rotate(180deg) !important;
        }
        html[data-sutra-popup-theme="dark"].sutra-popup-force-invert img,
        html[data-sutra-popup-theme="dark"].sutra-popup-force-invert picture,
        html[data-sutra-popup-theme="dark"].sutra-popup-force-invert video,
        html[data-sutra-popup-theme="dark"].sutra-popup-force-invert canvas,
        html[data-sutra-popup-theme="dark"].sutra-popup-force-invert svg,
        html[data-sutra-popup-theme="dark"].sutra-popup-force-invert iframe {
          filter: invert(1) hue-rotate(180deg) !important;
        }
      `;

      // Measure the page before our stylesheet is activated. This avoids
      // double-darkening pages that already provide their own dark palette.
      root.removeAttribute('data-sutra-popup-theme');
      root.classList.remove('sutra-popup-force-invert');

      function parseColor(value) {
        const match = String(value || '').match(/rgba?\s*\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)(?:\s*,\s*([\d.]+))?\s*\)/i);
        if (!match) return null;
        const alpha = match[4] === undefined ? 1 : Number(match[4]);
        if (alpha < 0.05) return null;
        return [Number(match[1]), Number(match[2]), Number(match[3])];
      }

      function luminance(rgb) {
        if (!rgb) return 1;
        return (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) / 255;
      }

      const body = document.body;
      const bodyColor = body ? parseColor(getComputedStyle(body).backgroundColor) : null;
      const rootColor = parseColor(getComputedStyle(root).backgroundColor);
      const effectiveColor = bodyColor || rootColor;
      const shouldInvert = luminance(effectiveColor) > 0.58;

      root.setAttribute('data-sutra-popup-theme', 'dark');

      // Only invert pages that are effectively light. Pages that already supply
      // a dark stylesheet keep their own colors and only receive the dark canvas.
      if (shouldInvert) {
        root.classList.add('sutra-popup-force-invert');
      }
    })();
  )JS" );
}

void applySutraPopupThemeToArticleView( ArticleView * view )
{
  if ( !view || !view->page() ) {
    return;
  }

  const bool dark = sutraPopupUsesDarkTheme();
  const bool monochrome = view->property( "sutraPopupPrimaryArticleTab" ).toBool()
                          || sutraPopupUsesCustomTheme();
  const QColor background = monochrome ? sutraPopupActiveBackgroundColor()
                                       : ( dark ? QColor( 11, 18, 32 ) : QColor( 255, 255, 255 ) );

  view->page()->setBackgroundColor( background );
  view->page()->runJavaScript( sutraPopupArticleThemeScript( dark, monochrome ) );
}

QString sutraPopupCornerToolsThemeStyleSheet()
{
  if ( sutraPopupUsesCustomTheme() ) {
    const QColor background = loadSutraPopupCustomBackgroundColor();
    const QColor text = loadSutraPopupCustomTextColor();
    const QColor border = sutraPopupBorderColor( background, text );
    const QColor hover = sutraPopupRaisedSurfaceColor( background );

    return QStringLiteral(
      "QWidget#sutraPopupCornerTools {"
      "  background: %1;"
      "  border: 1px solid %2;"
      "  border-radius: 4px;"
      "  color: %3;"
      "}"
      "QToolButton {"
      "  min-width: 22px;"
      "  min-height: 20px;"
      "  padding: 1px;"
      "  border: 0;"
      "  background: transparent;"
      "  color: %3;"
      "  font-weight: bold;"
      "}"
      "QToolButton:hover {"
      "  background: %4;"
      "  border-radius: 3px;"
      "}"
      "QToolButton:pressed { background: %4; padding-top: 2px; }"
      "QToolButton:focus { border: 1px solid %2; border-radius: 3px; }"
      "QToolButton:disabled { color: %2; }"
      "QFrame[sutraToolbarSeparator=\"true\"] {"
      "  background: %2; border: 0; min-width: 1px; max-width: 1px; margin: 4px 2px;"
      "}" ).arg( background.name( QColor::HexRgb ),
                    border.name( QColor::HexRgb ),
                    text.name( QColor::HexRgb ),
                    hover.name( QColor::HexRgb ) );
  }

  if ( sutraPopupUsesDarkTheme() ) {
    return QStringLiteral(
      "QWidget#sutraPopupCornerTools {"
      "  background: #0f172a;"
      "  border: 1px solid #64748b;"
      "  border-radius: 4px;"
      "  color: #e5e7eb;"
      "}"
      "QToolButton {"
      "  min-width: 22px;"
      "  min-height: 20px;"
      "  padding: 1px;"
      "  border: 0;"
      "  background: transparent;"
      "  color: #e5e7eb;"
      "  font-weight: bold;"
      "}"
      "QToolButton:hover {"
      "  background: rgba(96, 165, 250, 70);"
      "  border-radius: 3px;"
      "}"
      "QToolButton:pressed { background: rgba(96, 165, 250, 105); padding-top: 2px; }"
      "QToolButton:focus { border: 1px solid #60a5fa; border-radius: 3px; }"
      "QToolButton:disabled { color: #64748b; }"
      "QFrame[sutraToolbarSeparator=\"true\"] {"
      "  background: #475569; border: 0; min-width: 1px; max-width: 1px; margin: 4px 2px;"
      "}" );
  }

  return QStringLiteral(
    "QWidget#sutraPopupCornerTools {"
    "  background: #f5f8fc;"
    "  border: 1px solid #787878;"
    "  border-radius: 4px;"
    "  color: #0f172a;"
    "}"
    "QToolButton {"
    "  min-width: 22px;"
    "  min-height: 20px;"
    "  padding: 1px;"
    "  border: 0;"
    "  background: transparent;"
    "  color: #0f172a;"
    "  font-weight: bold;"
    "}"
    "QToolButton:hover {"
    "  background: rgba(80, 140, 220, 60);"
    "  border-radius: 3px;"
    "}"
    "QToolButton:pressed { background: rgba(80, 140, 220, 95); padding-top: 2px; }"
    "QToolButton:focus { border: 1px solid #2563eb; border-radius: 3px; }"
    "QToolButton:disabled { color: #94a3b8; }"
    "QFrame[sutraToolbarSeparator=\"true\"] {"
    "  background: #cbd5e1; border: 0; min-width: 1px; max-width: 1px; margin: 4px 2px;"
    "}" );
}

QString sutraPopupThemeStyleSheet()
{
  if ( sutraPopupUsesCustomTheme() ) {
    const QColor background = loadSutraPopupCustomBackgroundColor();
    const QColor text = loadSutraPopupCustomTextColor();
    const QColor surface = sutraPopupRaisedSurfaceColor( background );
    const QColor border = sutraPopupBorderColor( background, text );

    return QStringLiteral(
      "QMainWindow { background: %1; color: %2; }"
      "QToolBar, QStatusBar { background: %1; color: %2; border-color: %4; }"
      "QTabWidget::pane { background: %1; border: 1px solid %4; top: -1px; }"
      "QTabWidget::tab-bar { left: 6px; }"
      "QTabBar { qproperty-drawBase: 0; }"
      "QTabBar::tab {"
      "  background: %3; color: %2; border: 1px solid %4; border-bottom: 0;"
      "  border-top-left-radius: 5px; border-top-right-radius: 5px;"
      "  padding: 6px 12px; margin-right: 4px; min-width: 72px; min-height: 22px;"
      "}"
      "QTabBar::tab:selected { background: %1; color: %2; margin-bottom: -1px; }"
      "QTabBar::tab:hover:!selected { background: %3; }"
      "QTabBar::close-button { margin-left: 7px; subcontrol-position: right; }"
      "QLineEdit, QComboBox, QListView, QTreeView, QTableView, QTextEdit, QTextBrowser {"
      "  background: %1; color: %2; border: 1px solid %4; selection-background-color: %2;"
      "  selection-color: %1; }"
      "QToolButton, QPushButton { color: %2; background: transparent; }"
      "QToolButton:hover, QPushButton:hover { background: %3; }"
      "QMenu { background: %1; color: %2; border: 1px solid %4; }"
      "QMenu::item:selected { background: %3; color: %2; }"
      "QMenu::separator { background: %4; }"
      "QScrollBar:vertical, QScrollBar:horizontal { background: %1; border: 0; }"
      "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: %4; border-radius: 4px; }"
      "QToolTip { background: %1; color: %2; border: 1px solid %4; }"
    ).arg( background.name( QColor::HexRgb ),
           text.name( QColor::HexRgb ),
           surface.name( QColor::HexRgb ),
           border.name( QColor::HexRgb ) );
  }

  if ( sutraPopupUsesDarkTheme() ) {
    return QStringLiteral(
      "QMainWindow { background: #0b1220; color: #e5e7eb; }"
      "QToolBar, QStatusBar { background: #111827; color: #e5e7eb; border-color: #334155; }"
      "QTabWidget::pane { background: #0b1220; border: 1px solid #334155; top: -1px; }"
      "QTabWidget::tab-bar { left: 6px; }"
      "QTabBar { qproperty-drawBase: 0; }"
      "QTabBar::tab {"
      "  background: #1e293b; color: #cbd5e1; border: 1px solid #334155; border-bottom: 0;"
      "  border-top-left-radius: 5px; border-top-right-radius: 5px;"
      "  padding: 6px 12px; margin-right: 4px; min-width: 72px; min-height: 22px;"
      "}"
      "QTabBar::tab:selected { background: #0f172a; color: #f8fafc; margin-bottom: -1px; }"
      "QTabBar::tab:hover:!selected { background: #263449; }"
      "QTabBar::close-button { margin-left: 7px; subcontrol-position: right; }"
      "QLineEdit, QComboBox, QListView, QTreeView, QTableView, QTextEdit, QTextBrowser {"
      "  background: #111827; color: #e5e7eb; border: 1px solid #334155; selection-background-color: #1d4ed8;"
      "  selection-color: #ffffff; }"
      "QToolButton, QPushButton { color: #e5e7eb; background: transparent; }"
      "QToolButton:hover, QPushButton:hover { background: #1e293b; }"
      "QMenu { background: #111827; color: #e5e7eb; border: 1px solid #475569; }"
      "QMenu::item:selected { background: #1e3a5f; color: #ffffff; }"
      "QMenu::separator { background: #334155; }"
      "QScrollBar:vertical, QScrollBar:horizontal { background: #0f172a; border: 0; }"
      "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: #475569; border-radius: 4px; }"
      "QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #64748b; }"
      "QToolTip { background: #111827; color: #f8fafc; border: 1px solid #475569; }" );
  }

  return QStringLiteral(
    "QMainWindow { background: #f8fafc; color: #0f172a; }"
    "QToolBar, QStatusBar { background: #f8fafc; color: #0f172a; }"
    "QTabWidget::pane { background: #ffffff; border: 1px solid #cbd5e1; top: -1px; }"
    "QTabWidget::tab-bar { left: 6px; }"
    "QTabBar { qproperty-drawBase: 0; }"
    "QTabBar::tab {"
    "  background: #f1f5f9; color: #334155; border: 1px solid #cbd5e1; border-bottom: 0;"
    "  border-top-left-radius: 5px; border-top-right-radius: 5px;"
    "  padding: 6px 12px; margin-right: 4px; min-width: 72px; min-height: 22px;"
    "}"
    "QTabBar::tab:selected { background: #ffffff; color: #0f172a; margin-bottom: -1px; }"
    "QTabBar::tab:hover:!selected { background: #e2e8f0; }"
    "QTabBar::close-button { margin-left: 7px; subcontrol-position: right; }"
    "QLineEdit, QComboBox, QListView, QTreeView, QTableView, QTextEdit, QTextBrowser {"
    "  background: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; selection-background-color: #2563eb;"
    "  selection-color: #ffffff; }"
    "QMenu { background: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; }"
    "QMenu::item:selected { background: #dbeafe; color: #0f172a; }"
    "QMenu::separator { background: #e2e8f0; }" );
}

void updateSutraPopupThemeToggleButton( QWidget * popup )
{
  if ( !popup ) {
    return;
  }

  if ( QToolButton * button =
         popup->findChild< QToolButton * >( sutraPopupThemeToggleButtonObjectName() ) ) {
    const SutraPopupThemeMode mode = loadSutraPopupThemeMode();
    button->setText( QString() );
    button->setIcon( sutraPopupThemeToggleIcon( mode, sutraPopupActiveTextColor() ) );
    button->setIconSize( QSize( 20, 20 ) );
    button->setToolButtonStyle( Qt::ToolButtonIconOnly );

    if ( mode == SutraPopupThemeMode::Custom ) {
      button->setToolTip( QObject::tr( "Custom popup colors are active. Click to switch to Light mode." ) );
    }
    else {
      const bool dark = mode == SutraPopupThemeMode::Dark;
      button->setToolTip( dark ? QObject::tr( "Switch popup to Light mode (Ctrl+Shift+L)" )
                               : QObject::tr( "Switch popup to Dark mode (Ctrl+Shift+L)" ) );
    }
  }

  if ( QToolButton * colorsButton =
         popup->findChild< QToolButton * >( sutraPopupColorsButtonObjectName() ) ) {
    colorsButton->setIcon( sutraPopupColorSwatchIcon( sutraPopupActiveBackgroundColor() ) );
    colorsButton->setToolTip( QObject::tr( "Popup text and background colors" ) );
  }

  const QColor iconColor = sutraPopupActiveTextColor();
  if ( QToolButton * back = popup->findChild< QToolButton * >( QStringLiteral( "sutraHistoryBackButton" ) ) ) {
    back->setText( QString() );
    back->setIcon( sutraPopupNavigationIcon( false, sutraPopupToolbarIconColor( back->isEnabled() ) ) );
    back->setIconSize( QSize( 18, 18 ) );
  }
  if ( QToolButton * forward = popup->findChild< QToolButton * >( QStringLiteral( "sutraHistoryForwardButton" ) ) ) {
    forward->setText( QString() );
    forward->setIcon( sutraPopupNavigationIcon( true, sutraPopupToolbarIconColor( forward->isEnabled() ) ) );
    forward->setIconSize( QSize( 18, 18 ) );
  }
  if ( QToolButton * history = popup->findChild< QToolButton * >( QStringLiteral( "sutraHistoryButton" ) ) ) {
    history->setText( QString() );
    history->setIcon( sutraPopupHistoryIcon( iconColor ) );
    history->setIconSize( QSize( 18, 18 ) );
  }
  if ( QToolButton * note = popup->findChild< QToolButton * >( QStringLiteral( "sutraNoteButton" ) ) ) {
    const QString term = popup->property( "sutraCurrentLookupTerm" ).toString();
    const QVariantMap notes = QSettings().value( QStringLiteral( "SutraEdition/PopupLookupNotes" ) ).toMap();
    const bool hasNote = !term.isEmpty() && !notes.value( term ).toString().isEmpty();
    note->setText( QString() );
    note->setIcon( sutraPopupNoteIcon( sutraPopupToolbarIconColor( note->isEnabled() ), hasNote ) );
    note->setIconSize( QSize( 18, 18 ) );
  }
  if ( QToolButton * backToTop = popup->findChild< QToolButton * >( QStringLiteral( "sutraPopupBackToTopButton" ) ) ) {
    backToTop->setText( QString() );
    backToTop->setIcon( sutraPopupBackToTopIcon( iconColor ) );
    backToTop->setIconSize( QSize( 18, 18 ) );
  }
  if ( QToolButton * options = popup->findChild< QToolButton * >( QStringLiteral( "sutraPopupOptionsButton" ) ) ) {
    options->setText( QString() );
    options->setIcon( sutraPopupSettingsIcon( iconColor ) );
    options->setIconSize( QSize( 20, 20 ) );
  }
  if ( QToolButton * quickFix = popup->findChild< QToolButton * >( QStringLiteral( "sutraPopupQuickFixButton" ) ) ) {
    quickFix->setText( QString() );
    quickFix->setIcon( sutraPopupFixedLayoutIcon( iconColor ) );
    quickFix->setIconSize( QSize( 18, 18 ) );
  }
  if ( QAbstractButton * mainSettings = popup->findChild< QAbstractButton * >( QStringLiteral( "pinButton" ) ) ) {
    mainSettings->setText( QString() );
    mainSettings->setIcon( sutraPopupSettingsIcon( iconColor ) );
    mainSettings->setIconSize( QSize( 20, 20 ) );
  }
}

void applySutraPointingCursorToObject( QObject * object )
{
  if ( QAbstractButton * button = qobject_cast< QAbstractButton * >( object ) ) {
    button->setCursor( Qt::PointingHandCursor );
  }
  else if ( QTabBar * tabBar = qobject_cast< QTabBar * >( object ) ) {
    tabBar->setCursor( Qt::PointingHandCursor );
  }
  else if ( QMenu * menu = qobject_cast< QMenu * >( object ) ) {
    menu->setCursor( Qt::PointingHandCursor );
  }
  else if ( QComboBox * comboBox = qobject_cast< QComboBox * >( object ) ) {
    comboBox->setCursor( Qt::PointingHandCursor );
  }
}

void applySutraPopupPointingCursors( QWidget * popup )
{
  if ( !popup ) {
    return;
  }

  // Keep text editors and article content on their native cursor. Only controls
  // that are directly clickable receive the pointing-hand cursor.
  applySutraPointingCursorToObject( popup );
  for ( QObject * object : popup->findChildren< QObject * >() ) {
    applySutraPointingCursorToObject( object );
  }
}

void applySutraPopupTheme( QWidget * popup, QTabWidget * tabs )
{
  if ( !popup ) {
    return;
  }

  const char * baseStyleProperty = "sutraPopupBaseStyleSheet";
  if ( !popup->property( baseStyleProperty ).isValid() ) {
    popup->setProperty( baseStyleProperty, popup->styleSheet() );
  }

  const bool dark = sutraPopupUsesDarkTheme();
  popup->setProperty( "sutraPopupDarkTheme", dark );

  QPalette palette = QApplication::palette();
  if ( sutraPopupUsesCustomTheme() ) {
    const QColor background = loadSutraPopupCustomBackgroundColor();
    const QColor text = loadSutraPopupCustomTextColor();
    const QColor surface = sutraPopupRaisedSurfaceColor( background );
    const QColor border = sutraPopupBorderColor( background, text );

    palette.setColor( QPalette::Window, background );
    palette.setColor( QPalette::WindowText, text );
    palette.setColor( QPalette::Base, background );
    palette.setColor( QPalette::AlternateBase, surface );
    palette.setColor( QPalette::Text, text );
    palette.setColor( QPalette::Button, surface );
    palette.setColor( QPalette::ButtonText, text );
    palette.setColor( QPalette::Highlight, text );
    palette.setColor( QPalette::HighlightedText, background );
    palette.setColor( QPalette::ToolTipBase, background );
    palette.setColor( QPalette::ToolTipText, text );
    palette.setColor( QPalette::Mid, border );
  }
  else if ( dark ) {
    palette.setColor( QPalette::Window, QColor( 11, 18, 32 ) );
    palette.setColor( QPalette::WindowText, QColor( 229, 231, 235 ) );
    palette.setColor( QPalette::Base, QColor( 17, 24, 39 ) );
    palette.setColor( QPalette::AlternateBase, QColor( 30, 41, 59 ) );
    palette.setColor( QPalette::Text, QColor( 229, 231, 235 ) );
    palette.setColor( QPalette::Button, QColor( 30, 41, 59 ) );
    palette.setColor( QPalette::ButtonText, QColor( 229, 231, 235 ) );
    palette.setColor( QPalette::Highlight, QColor( 37, 99, 235 ) );
    palette.setColor( QPalette::HighlightedText, QColor( 255, 255, 255 ) );
    palette.setColor( QPalette::ToolTipBase, QColor( 17, 24, 39 ) );
    palette.setColor( QPalette::ToolTipText, QColor( 248, 250, 252 ) );
  }
  else {
    // Keep popup Light mode independent when the main application uses Dark mode.
    palette.setColor( QPalette::Window, QColor( 248, 250, 252 ) );
    palette.setColor( QPalette::WindowText, QColor( 15, 23, 42 ) );
    palette.setColor( QPalette::Base, QColor( 255, 255, 255 ) );
    palette.setColor( QPalette::AlternateBase, QColor( 241, 245, 249 ) );
    palette.setColor( QPalette::Text, QColor( 15, 23, 42 ) );
    palette.setColor( QPalette::Button, QColor( 248, 250, 252 ) );
    palette.setColor( QPalette::ButtonText, QColor( 15, 23, 42 ) );
    palette.setColor( QPalette::Highlight, QColor( 37, 99, 235 ) );
    palette.setColor( QPalette::HighlightedText, QColor( 255, 255, 255 ) );
    palette.setColor( QPalette::ToolTipBase, QColor( 255, 255, 255 ) );
    palette.setColor( QPalette::ToolTipText, QColor( 15, 23, 42 ) );
  }

  popup->setPalette( palette );
  popup->setAutoFillBackground( true );
  popup->setStyleSheet( popup->property( baseStyleProperty ).toString() + sutraPopupThemeStyleSheet() );

  if ( QWidget * tools = popup->findChild< QWidget * >( QStringLiteral( "sutraPopupCornerTools" ) ) ) {
    tools->setStyleSheet( sutraPopupCornerToolsThemeStyleSheet() );
  }

  updateSutraPopupThemeToggleButton( popup );

  if ( tabs ) {
    for ( int i = 0; i < tabs->count(); ++i ) {
      QWidget * tab = tabs->widget( i );

      if ( QTextBrowser * browser = qobject_cast< QTextBrowser * >( tab ) ) {
        applySutraPopupTextBrowserFont( browser );
      }
      else if ( ArticleView * view = qobject_cast< ArticleView * >( tab ) ) {
        view->setProperty( "sutraPopupPrimaryArticleTab", i == 0 );
        applySutraPopupThemeToArticleView( view );
      }
    }
  }

  popup->update();
}


enum class SutraMouseLookupMode {
  Disabled       = 0,
  CtrlRightClick = 1,
  CtrlLeftClick  = 2,
  AltRightClick  = 3,
  Custom         = 4
};

enum class SutraMouseLookupCaptureMode {
  Automatic    = 0,
  SelectedText = 1,
  EntirePhrase = 2
};

QString sutraMouseLookupModeSettingsKey()
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

constexpr int sutraMouseLookupDefaultMode = static_cast< int >( SutraMouseLookupMode::CtrlRightClick );
constexpr int sutraMouseLookupCtrlModifier = 0x01;
constexpr int sutraMouseLookupAltModifier = 0x02;

SutraMouseLookupMode loadSutraMouseLookupMode()
{
  QSettings settings;

  if ( settings.contains( sutraMouseLookupEnabledSettingsKey() ) ) {
    if ( !settings.value( sutraMouseLookupEnabledSettingsKey(), true ).toBool() ) {
      return SutraMouseLookupMode::Disabled;
    }

    const int modifiers = settings.value( sutraMouseLookupModifiersSettingsKey(),
                                          sutraMouseLookupCtrlModifier ).toInt();
    const int button = settings.value( sutraMouseLookupButtonSettingsKey(), 2 ).toInt();

    if ( modifiers == sutraMouseLookupCtrlModifier && button == 2 ) {
      return SutraMouseLookupMode::CtrlRightClick;
    }
    if ( modifiers == sutraMouseLookupCtrlModifier && button == 1 ) {
      return SutraMouseLookupMode::CtrlLeftClick;
    }
    if ( modifiers == sutraMouseLookupAltModifier && button == 2 ) {
      return SutraMouseLookupMode::AltRightClick;
    }

    return SutraMouseLookupMode::Custom;
  }

  const int value =
    qBound( 0, settings.value( sutraMouseLookupModeSettingsKey(), sutraMouseLookupDefaultMode ).toInt(), 3 );
  return static_cast< SutraMouseLookupMode >( value );
}

void saveSutraMouseLookupMode( SutraMouseLookupMode mode )
{
  QSettings settings;
  settings.setValue( sutraMouseLookupModeSettingsKey(), static_cast< int >( mode ) );

  switch ( mode ) {
    case SutraMouseLookupMode::Disabled:
      settings.setValue( sutraMouseLookupEnabledSettingsKey(), false );
      settings.setValue( sutraMouseLookupModifiersSettingsKey(), sutraMouseLookupCtrlModifier );
      settings.setValue( sutraMouseLookupButtonSettingsKey(), 2 );
      break;
    case SutraMouseLookupMode::CtrlLeftClick:
      settings.setValue( sutraMouseLookupEnabledSettingsKey(), true );
      settings.setValue( sutraMouseLookupModifiersSettingsKey(), sutraMouseLookupCtrlModifier );
      settings.setValue( sutraMouseLookupButtonSettingsKey(), 1 );
      break;
    case SutraMouseLookupMode::AltRightClick:
      settings.setValue( sutraMouseLookupEnabledSettingsKey(), true );
      settings.setValue( sutraMouseLookupModifiersSettingsKey(), sutraMouseLookupAltModifier );
      settings.setValue( sutraMouseLookupButtonSettingsKey(), 2 );
      break;
    case SutraMouseLookupMode::CtrlRightClick:
    default:
      settings.setValue( sutraMouseLookupEnabledSettingsKey(), true );
      settings.setValue( sutraMouseLookupModifiersSettingsKey(), sutraMouseLookupCtrlModifier );
      settings.setValue( sutraMouseLookupButtonSettingsKey(), 2 );
      break;
  }
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
    case SutraMouseLookupMode::Custom:
      return QStringLiteral( "Custom - Preferences > Hotkeys" );
    case SutraMouseLookupMode::CtrlRightClick:
    default:
      return QStringLiteral( "Ctrl + Right Click" );
  }
}

SutraMouseLookupCaptureMode loadSutraMouseLookupCaptureMode()
{
  QSettings settings;
  const int value = settings.value( sutraMouseLookupCaptureModeSettingsKey(),
                                    static_cast< int >( SutraMouseLookupCaptureMode::Automatic ) ).toInt();
  if ( value == static_cast< int >( SutraMouseLookupCaptureMode::SelectedText ) ) {
    return SutraMouseLookupCaptureMode::SelectedText;
  }
  if ( value == static_cast< int >( SutraMouseLookupCaptureMode::EntirePhrase ) ) {
    return SutraMouseLookupCaptureMode::EntirePhrase;
  }
  return SutraMouseLookupCaptureMode::Automatic;
}

void saveSutraMouseLookupCaptureMode( SutraMouseLookupCaptureMode mode )
{
  QSettings settings;
  settings.setValue( sutraMouseLookupCaptureModeSettingsKey(), static_cast< int >( mode ) );
}

QString sutraMouseLookupCaptureModeLabel( SutraMouseLookupCaptureMode mode )
{
  switch ( mode ) {
    case SutraMouseLookupCaptureMode::SelectedText:
      return QStringLiteral( "Manual - selected text only" );
    case SutraMouseLookupCaptureMode::EntirePhrase:
      return QStringLiteral( "Automatic - entire phrase" );
    case SutraMouseLookupCaptureMode::Automatic:
    default:
      return QStringLiteral( "Automatic - precise phrase" );
  }
}

void resetSutraPopupAppearanceDefaults()
{
  QSettings settings;
  settings.setValue( sutraPopupLayoutModeSettingsKey(), QStringLiteral( "auto" ) );
  settings.remove( sutraPopupFixedGeometrySettingsKey() );
  settings.setValue( sutraPopupFontSizeSettingsKey(), sutraPopupDefaultFontSize );
  settings.setValue( sutraPopupOpacitySettingsKey(), sutraPopupDefaultOpacityPercent );
  settings.setValue( sutraPopupThemeSettingsKey(), sutraPopupDefaultThemeMode );
  settings.setValue( sutraPopupShowGlossaryTabSettingsKey(), sutraPopupDefaultShowGlossaryTab );
  settings.setValue( sutraPopupShowWebTabSettingsKey(), sutraPopupDefaultShowWebTab );
  settings.remove( sutraPopupCustomTextColorSettingsKey() );
  settings.remove( sutraPopupCustomBackgroundColorSettingsKey() );
  settings.setValue( sutraMouseLookupModeSettingsKey(), sutraMouseLookupDefaultMode );
  settings.setValue( sutraMouseLookupEnabledSettingsKey(), true );
  settings.setValue( sutraMouseLookupModifiersSettingsKey(), sutraMouseLookupCtrlModifier );
  settings.setValue( sutraMouseLookupButtonSettingsKey(), 2 );
  settings.setValue( sutraMouseLookupCaptureModeSettingsKey(),
                     static_cast< int >( SutraMouseLookupCaptureMode::Automatic ) );
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

void updateSutraPopupCornerToolsResponsive( QWidget * popup )
{
  if ( !popup ) {
    return;
  }

  const int width = popup->width();
  const bool showNavigation = width >= 430;
  const bool showZoom = width >= 430;
  const bool showExtended = width >= 620;

  const auto setVisible = [ popup ]( const char * objectName, bool visible ) {
    if ( QWidget * widget = popup->findChild< QWidget * >( QString::fromLatin1( objectName ) ) ) {
      widget->setVisible( visible );
    }
  };

  setVisible( "sutraHistoryBackButton", showNavigation );
  setVisible( "sutraHistoryForwardButton", showNavigation );
  setVisible( "sutraPopupZoomOutButton", showZoom );
  setVisible( "sutraPopupZoomIndicator", showZoom );
  setVisible( "sutraPopupZoomInButton", showZoom );
  setVisible( "sutraPopupColorsButton", showExtended );
  setVisible( "sutraPopupQuickFixButton", showExtended );
  setVisible( "sutraPopupQuickFitButton", showExtended );

  setVisible( "sutraSeparatorHistoryZoom", showZoom );
  setVisible( "sutraSeparatorZoomAppearance", showZoom );
  setVisible( "sutraSeparatorAppearanceLayout", true );

  if ( QToolButton * options = popup->findChild< QToolButton * >( QStringLiteral( "sutraPopupOptionsButton" ) ) ) {
    options->setToolTip( showExtended ? QObject::tr( "Popup settings" )
                                      : QObject::tr( "Popup settings and hidden compact controls" ) );
  }
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

  updateSutraPopupCornerToolsResponsive( popup );
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

  // Passage mode: keep only the longest non-overlapping glossary chunk at
  // each position. This prevents a long sutra sentence from collapsing to
  // the first short term and also removes nested duplicates such as
  // "般若", "般若波羅蜜多" inside a longer matching phrase.
  QStringList terms;
  int coveredUntil = -1;

  for ( const Match & match : matches ) {
    if ( match.position < coveredUntil ) {
      continue;
    }

    if ( !terms.contains( match.term ) ) {
      terms << match.term;
    }

    coveredUntil = match.position + match.length;
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


static QString sutraPolishGlossaryFinalUxV6( QString html )
{
  // sutraGlossaryFinalUxV6
  // Final UI polish is applied to the full generated HTML, not only to small card fragments.
  // This is more reliable because the QTextBrowser rich text engine supports a limited CSS subset.

  html.replace( QStringLiteral( "Ph&#7853;t h&#7885;c / Buddhist Glossary" ),
                QStringLiteral( "Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c" ) );
  html.replace( QStringLiteral( "Pháº­t há»c / Buddhist Glossary" ),
                QStringLiteral( "Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c" ) );
  html.replace( QStringLiteral( "Buddhist Glossary" ),
                QStringLiteral( "Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c" ) );

  html.replace( QStringLiteral( "<h1>Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c</h1>" ),
                QStringLiteral( "<div style='margin:6px 0 4px 0;padding:0 0 4px 0;font-size:28px;line-height:1.25;font-weight:800;color:#0f172a;'>Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c</div>" ) );
  html.replace( QStringLiteral( "<h1>Giáº£i nghÄ©a Pháº­t há»c</h1>" ),
                QStringLiteral( "<div style='margin:6px 0 4px 0;padding:0 0 4px 0;font-size:28px;line-height:1.25;font-weight:800;color:#0f172a;'>Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c</div>" ) );

  html.replace( QStringLiteral( "K&#7871;t qu&#7843; nh&#7853;n di&#7879;n t&#7915; buddhist_terms.json, &#432;u ti&#234;n c&#7909;m thu&#7853;t ng&#7919; ch&#237;nh tr&#432;&#7899;c r&#7891;i m&#7899;i &#273;&#7871;n t&#7915; li&#234;n quan." ),
                QStringLiteral( "K&#7871;t qu&#7843; t&#7915; d&#7919; li&#7879;u thu&#7853;t ng&#7919; Ph&#7853;t h&#7885;c, &#432;u ti&#234;n c&#7909;m t&#7915; ch&#237;nh tr&#432;&#7899;c r&#7891;i &#273;&#7871;n t&#7915; li&#234;n quan." ) );

  html.replace( QStringLiteral( "<p>K&#7871;t qu&#7843; t&#7915; d&#7919; li&#7879;u thu&#7853;t ng&#7919; Ph&#7853;t h&#7885;c, &#432;u ti&#234;n c&#7909;m t&#7915; ch&#237;nh tr&#432;&#7899;c r&#7891;i &#273;&#7871;n t&#7915; li&#234;n quan.</p>" ),
                QStringLiteral( "<div style='margin:4px 0 16px 0;padding:10px 12px;background:#f8fafc;border-left:4px solid #38bdf8;color:#475569;font-size:14px;line-height:1.55;'>K&#7871;t qu&#7843; t&#7915; d&#7919; li&#7879;u thu&#7853;t ng&#7919; Ph&#7853;t h&#7885;c, &#432;u ti&#234;n c&#7909;m t&#7915; ch&#237;nh tr&#432;&#7899;c r&#7891;i &#273;&#7871;n t&#7915; li&#234;n quan.</div>" ) );

  html.replace( QStringLiteral( "<h2>K&#7871;t qu&#7843; ch&#237;nh</h2>" ),
                QStringLiteral( "<div style='margin:14px 0 10px 0;font-size:16px;line-height:1.2;font-weight:800;color:#0f766e;letter-spacing:.3px;'>K&#7871;t qu&#7843; ch&#237;nh</div><div style='margin:0 8px 18px 0;padding:18px 20px 20px 20px;background:#ffffff;border:1px solid #dbeafe;border-left:6px solid #0f766e;'>" ) );
  html.replace( QStringLiteral( "<h2>Káº¿t quáº£ chÃ­nh</h2>" ),
                QStringLiteral( "<div style='margin:14px 0 10px 0;font-size:16px;line-height:1.2;font-weight:800;color:#0f766e;letter-spacing:.3px;'>K&#7871;t qu&#7843; ch&#237;nh</div><div style='margin:0 8px 18px 0;padding:18px 20px 20px 20px;background:#ffffff;border:1px solid #dbeafe;border-left:6px solid #0f766e;'>" ) );

  html.replace( QStringLiteral( "<h2>T&#7915; li&#234;n quan</h2>" ),
                QStringLiteral( "</div><div style='margin:18px 0 10px 0;font-size:16px;line-height:1.2;font-weight:800;color:#7c2d12;letter-spacing:.3px;'>T&#7915; li&#234;n quan</div><div style='margin:0 8px 14px 0;padding:14px 16px;background:#fffdf8;border:1px solid #fed7aa;border-left:4px solid #f97316;'>" ) );
  html.replace( QStringLiteral( "<h2>Tá»« liÃªn quan</h2>" ),
                QStringLiteral( "</div><div style='margin:18px 0 10px 0;font-size:16px;line-height:1.2;font-weight:800;color:#7c2d12;letter-spacing:.3px;'>T&#7915; li&#234;n quan</div><div style='margin:0 8px 14px 0;padding:14px 16px;background:#fffdf8;border:1px solid #fed7aa;border-left:4px solid #f97316;'>" ) );

  html.replace( QStringLiteral( "<b>PINYIN</b>" ),
                QStringLiteral( "<div style='margin:14px 0 5px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>PINYIN</div>" ) );
  html.replace( QStringLiteral( "<strong>PINYIN</strong>" ),
                QStringLiteral( "<div style='margin:14px 0 5px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>PINYIN</div>" ) );

  html.replace( QStringLiteral( "<b>NGH&#296;A TI&#7870;NG VI&#7878;T</b>" ),
                QStringLiteral( "<div style='margin:14px 0 7px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>NGH&#296;A TI&#7870;NG VI&#7878;T</div>" ) );
  html.replace( QStringLiteral( "<b>NGHÄ¨A TIáº¾NG VIá»†T</b>" ),
                QStringLiteral( "<div style='margin:14px 0 7px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>NGH&#296;A TI&#7870;NG VI&#7878;T</div>" ) );

  html.replace( QStringLiteral( "<b>G&#7906;I &#221; D&#7882;CH</b>" ),
                QStringLiteral( "<div style='margin:14px 0 6px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>G&#7906;I &#221; D&#7882;CH</div>" ) );
  html.replace( QStringLiteral( "<b>Gá»¢I Ã Dá»ŠCH</b>" ),
                QStringLiteral( "<div style='margin:14px 0 6px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>G&#7906;I &#221; D&#7882;CH</div>" ) );

  html.replace( QStringLiteral( "<b>LI&#202;N QUAN</b>" ),
                QStringLiteral( "<div style='margin:14px 0 6px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>LI&#202;N QUAN</div>" ) );
  html.replace( QStringLiteral( "<b>LIÃŠN QUAN</b>" ),
                QStringLiteral( "<div style='margin:14px 0 6px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>LI&#202;N QUAN</div>" ) );

  html.replace( QStringLiteral( "<h1>" ),
                QStringLiteral( "<h1 style='font-size:32px;line-height:1.15;margin:4px 0 8px 0;color:#020617;font-weight:800;'>" ) );
  html.replace( QStringLiteral( "<h2>" ),
                QStringLiteral( "<h2 style='font-size:23px;line-height:1.2;margin:10px 0 5px 0;color:#0f172a;font-weight:800;'>" ) );
  html.replace( QStringLiteral( "<h3>" ),
                QStringLiteral( "<h3 style='font-size:21px;line-height:1.2;margin:10px 0 5px 0;color:#0f172a;font-weight:800;'>" ) );

  html.replace( QStringLiteral( "<a " ),
                QStringLiteral( "<a style='display:inline-block;margin:4px 7px 4px 0;padding:4px 9px;background:#fff7ed;border:1px solid #fed7aa;color:#9a3412;text-decoration:none;font-weight:700;' " ) );

  html.prepend( QStringLiteral( "<div style='padding:10px 12px 18px 12px;background:#f7f9fc;color:#0f172a;font-size:16px;line-height:1.55;'>" ) );
  html.append( QStringLiteral( "</div></div>" ) );

  return html;
}

QString glossaryHtmlFinalUxBaseV6( const QString & primaryTerm, const QStringList & detectedTerms )
;


static QString sutraPolishGlossaryFinalUxV7( QString html )
{
  // sutraGlossaryFinalUxV7
  // This polish runs on the final generated HTML. It is intentionally regex-based
  // because older patches changed the exact tag style, so exact QString::replace is too fragile.

  html.remove( QRegularExpression( QStringLiteral( R"GD(<style[\s\S]*?</style>)GD" ) ) );

  const QString outerOpen = QStringLiteral(
    "<div style='padding:10px 12px 18px 12px;background:#f7f9fc;color:#0f172a;font-size:16px;line-height:1.55;'>" );

  const QString titleBlock = QStringLiteral(
    "<div style='margin:6px 0 4px 0;padding:0 0 4px 0;font-size:28px;line-height:1.25;font-weight:800;color:#0f172a;'>"
    "Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c</div>" );

  const QString subtitleBlock = QStringLiteral(
    "<div style='margin:4px 0 16px 0;padding:10px 12px;background:#f8fafc;border-left:4px solid #38bdf8;"
    "color:#475569;font-size:14px;line-height:1.55;'>"
    "K&#7871;t qu&#7843; t&#7915; d&#7919; li&#7879;u thu&#7853;t ng&#7919; Ph&#7853;t h&#7885;c, "
    "&#432;u ti&#234;n c&#7909;m t&#7915; ch&#237;nh tr&#432;&#7899;c r&#7891;i &#273;&#7871;n t&#7915; li&#234;n quan.</div>" );

  html.replace( QRegularExpression( QStringLiteral( R"GD(<h1[^>]*>\s*(?:Pháº­t há»c / Buddhist Glossary|Ph&#7853;t h&#7885;c / Buddhist Glossary|Giáº£i nghÄ©a Pháº­t há»c|Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c|Buddhist Glossary)\s*</h1>)GD" ) ),
                titleBlock );

  html.replace( QStringLiteral( "Pháº­t há»c / Buddhist Glossary" ),
                QStringLiteral( "Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c" ) );
  html.replace( QStringLiteral( "Ph&#7853;t h&#7885;c / Buddhist Glossary" ),
                QStringLiteral( "Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c" ) );
  html.replace( QStringLiteral( "Buddhist Glossary" ),
                QStringLiteral( "Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c" ) );

  html.replace( QRegularExpression( QStringLiteral( R"GD(<p[^>]*>[^<]*buddhist_terms\.json[^<]*</p>)GD" ) ),
                subtitleBlock );
  html.replace( QRegularExpression( QStringLiteral( R"GD(<div[^>]*>[^<]*buddhist_terms\.json[^<]*</div>)GD" ) ),
                subtitleBlock );

  const QString primarySection = QStringLiteral(
    "<div style='margin:14px 0 10px 0;font-size:16px;line-height:1.2;font-weight:800;color:#0f766e;letter-spacing:.3px;'>"
    "K&#7871;t qu&#7843; ch&#237;nh</div>"
    "<div data-sutra-v7-primary='1' style='margin:0 8px 18px 0;padding:18px 20px 20px 20px;"
    "background:#ffffff;border:1px solid #dbeafe;border-left:6px solid #0f766e;'>" );

  const QString relatedSection = QStringLiteral(
    "</div>"
    "<div style='margin:18px 0 10px 0;font-size:16px;line-height:1.2;font-weight:800;color:#7c2d12;letter-spacing:.3px;'>"
    "T&#7915; li&#234;n quan</div>"
    "<div data-sutra-v7-related='1' style='margin:0 8px 14px 0;padding:14px 16px;"
    "background:#fffdf8;border:1px solid #fed7aa;border-left:4px solid #f97316;'>" );

  html.replace( QRegularExpression( QStringLiteral( R"GD(<h2[^>]*>\s*(?:Káº¿t quáº£ chÃ­nh|K&#7871;t qu&#7843; ch&#237;nh)\s*</h2>)GD" ) ),
                primarySection );

  html.replace( QRegularExpression( QStringLiteral( R"GD(<h2[^>]*>\s*(?:Tá»« liÃªn quan|T&#7915; li&#234;n quan)\s*</h2>)GD" ) ),
                relatedSection );

  const QString pinyinLabel = QStringLiteral(
    "<div style='margin:14px 0 5px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>PINYIN</div>" );

  const QString meaningLabel = QStringLiteral(
    "<div style='margin:14px 0 7px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>NGH&#296;A TI&#7870;NG VI&#7878;T</div>" );

  const QString suggestionLabel = QStringLiteral(
    "<div style='margin:14px 0 6px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>G&#7906;I &#221; D&#7882;CH</div>" );

  const QString relatedLabel = QStringLiteral(
    "<div style='margin:14px 0 6px 0;font-size:12px;line-height:1.1;font-weight:800;letter-spacing:1px;color:#64748b;'>LI&#202;N QUAN</div>" );

  html.replace( QRegularExpression( QStringLiteral( R"GD(<(?:b|strong)[^>]*>\s*PINYIN\s*</(?:b|strong)>)GD" ) ),
                pinyinLabel );
  html.replace( QRegularExpression( QStringLiteral( R"GD(<(?:b|strong)[^>]*>\s*(?:NGHÄ¨A TIáº¾NG VIá»†T|NGH&#296;A TI&#7870;NG VI&#7878;T)\s*</(?:b|strong)>)GD" ) ),
                meaningLabel );
  html.replace( QRegularExpression( QStringLiteral( R"GD(<(?:b|strong)[^>]*>\s*(?:Gá»¢I Ã Dá»ŠCH|G&#7906;I &#221; D&#7882;CH)\s*</(?:b|strong)>)GD" ) ),
                suggestionLabel );
  html.replace( QRegularExpression( QStringLiteral( R"GD(<(?:b|strong)[^>]*>\s*(?:LIÃŠN QUAN|LI&#202;N QUAN)\s*</(?:b|strong)>)GD" ) ),
                relatedLabel );

  html.replace( QRegularExpression( QStringLiteral( R"GD(<h1[^>]*>)GD" ) ),
                QStringLiteral( "<h1 style='font-size:32px;line-height:1.15;margin:4px 0 8px 0;color:#020617;font-weight:800;'>" ) );
  html.replace( QRegularExpression( QStringLiteral( R"GD(<h2[^>]*>)GD" ) ),
                QStringLiteral( "<h2 style='font-size:23px;line-height:1.2;margin:10px 0 5px 0;color:#0f172a;font-weight:800;'>" ) );
  html.replace( QRegularExpression( QStringLiteral( R"GD(<h3[^>]*>)GD" ) ),
                QStringLiteral( "<h3 style='font-size:21px;line-height:1.2;margin:10px 0 5px 0;color:#0f172a;font-weight:800;'>" ) );

  html.replace( QRegularExpression( QStringLiteral( R"GD(<a\s+)GD" ) ),
                QStringLiteral( "<a style='display:inline-block;margin:4px 7px 4px 0;padding:4px 9px;background:#fff7ed;border:1px solid #fed7aa;color:#9a3412;text-decoration:none;font-weight:700;' " ) );

  const bool hasPrimaryCard = html.contains( QStringLiteral( "data-sutra-v7-primary" ) );
  const bool hasRelatedCard = html.contains( QStringLiteral( "data-sutra-v7-related" ) );

  html.prepend( outerOpen );

  if ( hasPrimaryCard || hasRelatedCard ) {
    html.append( QStringLiteral( "</div>" ) );
  }

  html.append( QStringLiteral( "</div>" ) );

  return html;
}

QString glossaryHtml( const QString & primaryTerm, const QStringList & detectedTerms )

{
  return sutraPolishGlossaryFinalUxV7( glossaryHtmlFinalUxBaseV6( primaryTerm, detectedTerms ) );
}
QString glossaryHtmlFinalUxBaseV6( const QString & primaryTerm, const QStringList & detectedTerms )
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
    "body{font-family:'Segoe UI','Noto Sans','Arial',sans-serif;font-size:%1px;line-height:1.58;margin:0;padding:14px 14px 56px;background:#f6f8fb;color:#1f2937;}"
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
    "<div class='hero-title'>Gi&#7843;i ngh&#297;a Ph&#7853;t h&#7885;c</div>"
    "<p class='hero-subtitle'>K&#7871;t qu&#7843; t&#7915; d&#7919; li&#7879;u thu&#7853;t ng&#7919; Ph&#7853;t h&#7885;c, &#432;u ti&#234;n c&#7909;m t&#7915; ch&#237;nh tr&#432;&#7899;c r&#7891;i &#273;&#7871;n t&#7915; li&#234;n quan.</p>"
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


// sutraGlossaryStableUiV16
// Single-pass HTML renderer for the glossary tab.
// Keep this renderer self-contained so later UI polishing does not duplicate labels/sections.
QString sutraGlossaryChipHtmlV16( const QString & text, bool relatedStyle )
{
  const QString trimmed = text.trimmed();
  if ( trimmed.isEmpty() ) {
    return {};
  }

  const QString style = relatedStyle
                          ? QStringLiteral( "display:inline-block;margin:4px 7px 4px 0;padding:5px 10px;"
                                            "background:#fff7ed;border:1px solid #fdba74;border-radius:999px;"
                                            "color:#9a3412;font-weight:700;" )
                          : QStringLiteral( "display:inline-block;margin:4px 7px 4px 0;padding:5px 10px;"
                                            "background:#eff6ff;border:1px solid #bfdbfe;border-radius:999px;"
                                            "color:#1d4ed8;font-weight:700;" );

  return QStringLiteral( "<span style='%1'>%2</span>" ).arg( style, htmlEscape( trimmed ) );
}

QString glossaryHtmlStableV16( const QString & primaryTerm, const QStringList & detectedTerms )
{
  const QList< BuddhistGlossaryEntry > entries = glossaryEntriesForTerms( primaryTerm, detectedTerms );
  const int fontSize                           = loadSutraPopupFontSize();
  const int termSize                           = qMax( 25, fontSize + 10 );
  const int hanVietSize                        = qMax( 17, fontSize + 2 );
  const int labelSize                          = qMax( 11, fontSize - 3 );
  const int sectionSize                        = qMax( 15, fontSize );

  QString html;
  html += QStringLiteral( "<html><head><meta charset='utf-8'></head>"
                          "<body style='margin:0;padding:12px 14px 20px 14px;background:#f8fafc;"
                          "color:#0f172a;font-family:Segoe UI,Arial,sans-serif;font-size:%1px;line-height:1.55;'>" )
            .arg( fontSize );

  if ( entries.isEmpty() ) {
    html += QStringLiteral(
      "<div style='margin:2px 0 10px 0;padding:9px 12px;background:#f1f5f9;"
      "border-left:5px solid #64748b;font-size:%1px;font-weight:800;letter-spacing:.4px;color:#334155;'>"
      "K&#7870;T QU&#7842; TRA C&#7912;U</div>"
      "<div style='padding:16px;background:#ffffff;border:1px dashed #cbd5e1;border-radius:8px;'>"
      "<div style='font-size:%2px;font-weight:800;color:#0f172a;margin-bottom:6px;'>"
      "Ch&#432;a c&#243; thu&#7853;t ng&#7919; ph&#249; h&#7907;p</div>"
      "<div style='color:#475569;'>Kh&#244;ng t&#236;m th&#7845;y m&#7909;c Ph&#7853;t h&#7885;c n&#7897;i b&#7897; cho: "
      "<b>%3</b></div></div>" )
              .arg( sectionSize )
              .arg( qMax( 18, fontSize + 2 ) )
              .arg( htmlEscape( primaryTerm ) );

    html += QStringLiteral( "</body></html>" );
    return html;
  }

  const QString normalizedPrimary = normalizeSmartLookupInput( primaryTerm );
  BuddhistGlossaryEntry primaryEntry;
  bool primaryFound = false;

  for ( const BuddhistGlossaryEntry & entry : entries ) {
    if ( normalizeSmartLookupInput( entry.term ) == normalizedPrimary ) {
      primaryEntry = entry;
      primaryFound = true;
      break;
    }
  }

  if ( !primaryFound ) {
    primaryEntry = entries.first();
  }

  // When the captured text contains several consecutive glossary phrases,
  // render the whole passage instead of showing only the first term as the
  // main result and reducing the remaining phrases to related chips.
  if ( entries.size() > 1 ) {
    html += QStringLiteral(
      "<div style='margin:2px 0 10px 0;padding:9px 12px;background:#ecfdf5;"
      "border-left:5px solid #0f766e;font-size:%1px;font-weight:800;letter-spacing:.5px;color:#0f766e;'>"
      "PH&#194;N T&#205;CH &#272;O&#7840;N KINH</div>" )
              .arg( sectionSize );

    QStringList combinedTranslations;
    QStringList passageTerms;

    for ( int i = 0; i < entries.size(); ++i ) {
      const BuddhistGlossaryEntry & entry = entries.at( i );
      passageTerms << entry.term;

      QString preferredTranslation;
      if ( !entry.suggestedTranslations.isEmpty() ) {
        preferredTranslation = entry.suggestedTranslations.first().trimmed();
      }
      if ( preferredTranslation.isEmpty() ) {
        preferredTranslation = entry.meaningVi.trimmed();
      }
      if ( preferredTranslation.isEmpty() ) {
        preferredTranslation = entry.hanViet.trimmed();
      }

      if ( !preferredTranslation.isEmpty() && !combinedTranslations.contains( preferredTranslation ) ) {
        combinedTranslations << preferredTranslation;
      }

      html += QStringLiteral(
        "<div style='margin:0 0 10px 0;padding:13px 15px;background:#ffffff;"
        "border:1px solid #cbd5e1;border-left:5px solid #0f766e;border-radius:8px;'>"
        "<div style='font-size:%1px;font-weight:800;color:#64748b;letter-spacing:.5px;'>"
        "&#272;O&#7840;N %2</div>"
        "<div style='margin-top:4px;font-size:%3px;line-height:1.2;font-weight:800;color:#020617;'>%4</div>" )
                .arg( labelSize )
                .arg( i + 1 )
                .arg( qMax( 21, fontSize + 6 ) )
                .arg( htmlEscape( entry.term ) );

      if ( !entry.hanViet.isEmpty() ) {
        html += QStringLiteral(
          "<div style='margin-top:5px;font-size:%1px;font-weight:750;color:#0f766e;'>%2</div>" )
                  .arg( hanVietSize )
                  .arg( htmlEscape( entry.hanViet ) );
      }

      if ( !entry.pinyin.isEmpty() ) {
        html += QStringLiteral(
          "<div style='margin-top:5px;color:#64748b;font-style:italic;'>%1</div>" )
                  .arg( htmlEscape( entry.pinyin ) );
      }

      if ( !preferredTranslation.isEmpty() ) {
        html += QStringLiteral(
          "<div style='margin-top:9px;padding:9px 11px;background:#f1f5f9;border-radius:6px;"
          "color:#111827;'>%1</div>" )
                  .arg( htmlEscape( preferredTranslation ) );
      }

      html += QStringLiteral( "</div>" );
    }

    if ( !combinedTranslations.isEmpty() ) {
      QString combinedText = combinedTranslations.join( QStringLiteral( ", " ) ).trimmed();
      if ( !combinedText.endsWith( QLatin1Char( '.' ) )
        && !combinedText.endsWith( QChar( 0x3002 ) )
        && !combinedText.endsWith( QLatin1Char( '!' ) )
        && !combinedText.endsWith( QLatin1Char( '?' ) ) ) {
        combinedText += QLatin1Char( '.' );
      }

      html += QStringLiteral(
        "<div style='margin:14px 0 8px 0;padding:9px 12px;background:#eff6ff;"
        "border-left:5px solid #2563eb;font-size:%1px;font-weight:800;letter-spacing:.5px;color:#1d4ed8;'>"
        "B&#7842;N D&#7882;CH G&#7906;I &#221;</div>"
        "<div style='margin:0 0 14px 0;padding:13px 15px;background:#ffffff;border:1px solid #bfdbfe;"
        "border-left:5px solid #3b82f6;border-radius:8px;color:#0f172a;font-size:%2px;font-weight:650;'>%3</div>" )
                .arg( sectionSize )
                .arg( qMax( 17, fontSize + 1 ) )
                .arg( htmlEscape( combinedText ) );
    }

    QStringList passageRelated;
    const auto appendPassageRelated = [&]( const QString & value ) {
      const QString trimmed = value.trimmed();
      const QString normalized = normalizeSmartLookupInput( trimmed );
      if ( trimmed.isEmpty() || normalized.isEmpty() ) {
        return;
      }

      for ( const QString & term : passageTerms ) {
        if ( normalizeSmartLookupInput( term ) == normalized ) {
          return;
        }
      }

      for ( const QString & existing : passageRelated ) {
        if ( normalizeSmartLookupInput( existing ) == normalized ) {
          return;
        }
      }

      passageRelated << trimmed;
    };

    for ( const BuddhistGlossaryEntry & entry : entries ) {
      for ( const QString & related : entry.related ) {
        appendPassageRelated( related );
      }
    }

    if ( !passageRelated.isEmpty() ) {
      QStringList relatedChips;
      for ( const QString & related : passageRelated ) {
        const QString chip = sutraGlossaryChipHtmlV16( related, true );
        if ( !chip.isEmpty() ) {
          relatedChips << chip;
        }
      }

      html += QStringLiteral(
        "<div style='margin:4px 0 10px 0;padding:9px 12px;background:#fff7ed;"
        "border-left:5px solid #c2410c;font-size:%1px;font-weight:800;letter-spacing:.5px;color:#9a3412;'>"
        "LI&#202;N QUAN</div>"
        "<div style='margin:0 0 14px 0;padding:11px 13px;background:#ffffff;border:1px solid #fed7aa;"
        "border-left:4px solid #f97316;border-radius:8px;'>%2</div>" )
                .arg( sectionSize )
                .arg( relatedChips.join( QString() ) );
    }

    html += QStringLiteral(
      "<div style='margin-top:12px;color:#94a3b8;font-size:%1px;'>"
      "Ngu&#7891;n d&#7919; li&#7879;u: buddhist_terms.json</div>" )
              .arg( labelSize );

    html += QStringLiteral( "</body></html>" );
    return html;
  }

  html += QStringLiteral(
    "<div style='margin:2px 0 10px 0;padding:9px 12px;background:#ecfdf5;"
    "border-left:5px solid #0f766e;font-size:%1px;font-weight:800;letter-spacing:.5px;color:#0f766e;'>"
    "K&#7870;T QU&#7842; CH&#205;NH</div>" )
            .arg( sectionSize );

  html += QStringLiteral(
    "<div style='margin:0 0 16px 0;padding:15px 17px 17px 17px;background:#ffffff;"
    "border:1px solid #cbd5e1;border-left:6px solid #0f766e;border-radius:8px;'>" );

  html += QStringLiteral( "<div style='font-size:%1px;line-height:1.18;font-weight:800;color:#020617;'>%2</div>" )
            .arg( termSize )
            .arg( htmlEscape( primaryEntry.term ) );

  if ( !primaryEntry.hanViet.isEmpty() ) {
    html += QStringLiteral( "<div style='margin-top:5px;font-size:%1px;font-weight:700;color:#0f766e;'>%2</div>" )
              .arg( hanVietSize )
              .arg( htmlEscape( primaryEntry.hanViet ) );
  }

  if ( !primaryEntry.category.isEmpty() ) {
    html += QStringLiteral(
      "<div style='margin-top:8px;'><span style='display:inline-block;padding:3px 9px;"
      "background:#f0fdf4;border:1px solid #bbf7d0;border-radius:999px;color:#047857;"
      "font-size:%1px;font-weight:700;'>%2</span></div>" )
              .arg( labelSize )
              .arg( htmlEscape( primaryEntry.category ) );
  }

  if ( !primaryEntry.pinyin.isEmpty() ) {
    html += QStringLiteral(
      "<div style='margin-top:14px;padding-top:10px;border-top:1px solid #e2e8f0;'>"
      "<div style='font-size:%1px;font-weight:800;letter-spacing:1px;color:#64748b;'>PINYIN</div>"
      "<div style='margin-top:4px;color:#0f172a;'>%2</div></div>" )
              .arg( labelSize )
              .arg( htmlEscape( primaryEntry.pinyin ) );
  }

  if ( !primaryEntry.meaningVi.isEmpty() ) {
    html += QStringLiteral(
      "<div style='margin-top:13px;padding:11px 12px;background:#f1f5f9;border:1px solid #e2e8f0;"
      "border-radius:7px;'>"
      "<div style='font-size:%1px;font-weight:800;letter-spacing:1px;color:#475569;'>"
      "NGH&#296;A TI&#7870;NG VI&#7878;T</div>"
      "<div style='margin-top:6px;color:#111827;'>%2</div></div>" )
              .arg( labelSize )
              .arg( htmlEscape( primaryEntry.meaningVi ) );
  }

  if ( !primaryEntry.suggestedTranslations.isEmpty() ) {
    QStringList suggestionChips;
    for ( const QString & suggestion : primaryEntry.suggestedTranslations ) {
      const QString chip = sutraGlossaryChipHtmlV16( suggestion, false );
      if ( !chip.isEmpty() ) {
        suggestionChips << chip;
      }
    }

    if ( !suggestionChips.isEmpty() ) {
      html += QStringLiteral(
        "<div style='margin-top:13px;'>"
        "<div style='font-size:%1px;font-weight:800;letter-spacing:1px;color:#475569;'>"
        "G&#7906;I &#221; D&#7882;CH</div>"
        "<div style='margin-top:5px;'>%2</div></div>" )
                .arg( labelSize )
                .arg( suggestionChips.join( QString() ) );
    }
  }

  html += QStringLiteral( "</div>" );

  // Build one related-term list from both JSON relations and detected glossary entries.
  // The same relation is rendered only once, and related entries stay compact instead of
  // repeating the full PINYIN / MEANING / SUGGESTION section set.
  QStringList relatedTerms;
  const QString normalizedPrimaryEntry = normalizeSmartLookupInput( primaryEntry.term );

  const auto appendRelatedTerm = [&]( const QString & value ) {
    const QString trimmed = value.trimmed();
    const QString normalized = normalizeSmartLookupInput( trimmed );

    if ( trimmed.isEmpty() || normalized.isEmpty() || normalized == normalizedPrimaryEntry ) {
      return;
    }

    for ( const QString & existing : relatedTerms ) {
      if ( normalizeSmartLookupInput( existing ) == normalized ) {
        return;
      }
    }

    relatedTerms << trimmed;
  };

  for ( const QString & related : primaryEntry.related ) {
    appendRelatedTerm( related );
  }

  for ( const BuddhistGlossaryEntry & entry : entries ) {
    appendRelatedTerm( entry.term );
  }

  if ( !relatedTerms.isEmpty() ) {
    QStringList relatedChips;
    for ( const QString & related : relatedTerms ) {
      const QString chip = sutraGlossaryChipHtmlV16( related, true );
      if ( !chip.isEmpty() ) {
        relatedChips << chip;
      }
    }

    html += QStringLiteral(
      "<div style='margin:4px 0 10px 0;padding:9px 12px;background:#fff7ed;"
      "border-left:5px solid #c2410c;font-size:%1px;font-weight:800;letter-spacing:.5px;color:#9a3412;'>"
      "LI&#202;N QUAN</div>"
      "<div style='margin:0 0 14px 0;padding:11px 13px;background:#ffffff;border:1px solid #fed7aa;"
      "border-left:4px solid #f97316;border-radius:8px;'>%2</div>" )
              .arg( sectionSize )
              .arg( relatedChips.join( QString() ) );
  }

  html += QStringLiteral(
    "<div style='margin-top:12px;color:#94a3b8;font-size:%1px;'>"
    "Ngu&#7891;n d&#7919; li&#7879;u: buddhist_terms.json</div>" )
            .arg( labelSize );

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


// sutraFinalSafeWebGlossaryV13
static QString sutraSafeWebReferenceV13( QString html )
{
  // Use only ASCII + HTML numeric entities in generated labels to avoid Vietnamese mojibake.

  if ( html.contains( QStringLiteral( "data-sutra-web-v13" ) ) ) {
    return html;
  }

  QRegularExpression sectionRegex( QStringLiteral( R"GD(<h2[^>]*>([^<]+)</h2>\s*<ul>[\s\S]*?</ul>)GD" ) );
  QRegularExpressionMatchIterator it = sectionRegex.globalMatch( html );

  QString rebuilt;
  int cursor = 0;

  while ( it.hasNext() ) {
    const QRegularExpressionMatch match = it.next();
    const QString sectionTitle = match.captured( 1 ).trimmed();

    rebuilt += html.mid( cursor, match.capturedStart() - cursor );

    const QString encoded = QString::fromLatin1( QUrl::toPercentEncoding( sectionTitle ) );

    const QString viUrl = QStringLiteral( "https://vi.wikipedia.org/wiki/Special:Search?search=" ) + encoded;
    const QString zhUrl = QStringLiteral( "https://zh.wikipedia.org/wiki/Special:Search?search=" ) + encoded;
    const QString enUrl = QStringLiteral( "https://en.wikipedia.org/wiki/Special:Search?search=" ) + encoded;
    const QString wiktionaryUrl = QStringLiteral( "https://en.wiktionary.org/wiki/Special:Search?search=" ) + encoded;
    const QString googleUrl = QStringLiteral( "https://www.google.com/search?q=" ) + encoded;

    rebuilt += QStringLiteral(
      "<div style='margin:16px 0 8px 0;padding:7px 10px;background:#ffffff;border-left:4px solid #64748b;"
      "font-size:22px;line-height:1.25;font-weight:800;color:#0f172a;'>" )
      + sectionTitle
      + QStringLiteral( "</div>" );

    rebuilt += QStringLiteral( "<ul style='margin:8px 0 16px 28px;padding:0;font-size:17px;line-height:1.8;'>" );

    rebuilt += QStringLiteral( "<li><a style='font-weight:700;color:#2563eb;text-decoration:none;' href=\"" )
            + viUrl
            + QStringLiteral( "\">Wikipedia ti&#7871;ng Vi&#7879;t</a></li>" );

    rebuilt += QStringLiteral( "<li><a style='font-weight:700;color:#2563eb;text-decoration:none;' href=\"" )
            + zhUrl
            + QStringLiteral( "\">Wikipedia ti&#7871;ng Trung</a></li>" );

    rebuilt += QStringLiteral( "<li><a style='font-weight:700;color:#2563eb;text-decoration:none;' href=\"" )
            + enUrl
            + QStringLiteral( "\">Wikipedia ti&#7871;ng Anh</a></li>" );

    rebuilt += QStringLiteral( "<li><a style='font-weight:700;color:#2563eb;text-decoration:none;' href=\"" )
            + wiktionaryUrl
            + QStringLiteral( "\">Wiktionary</a></li>" );

    rebuilt += QStringLiteral( "<li><a style='font-weight:700;color:#2563eb;text-decoration:none;' href=\"" )
            + googleUrl
            + QStringLiteral( "\">Google Search</a></li>" );

    rebuilt += QStringLiteral( "</ul>" );

    cursor = match.capturedEnd();
  }

  if ( cursor > 0 ) {
    rebuilt += html.mid( cursor );
    html = rebuilt;
  }
  else {
    // Fallback only. Keep replacements ASCII-safe.
    html.replace( QStringLiteral( "ti&amp;#7871;ng" ), QStringLiteral( "ti&#7871;ng" ) );
    html.replace( QStringLiteral( "ti&#7871;ng" ), QStringLiteral( "ti&#7871;ng" ) );
  }

  html.prepend( QStringLiteral(
    "<div data-sutra-web-v13='1' style='padding:12px 14px 20px 14px;background:#f7f9fc;color:#0f172a;font-size:16px;line-height:1.55;'>" ) );
  html.append( QStringLiteral( "</div>" ) );

  return html;
}

static QString sutraSafeGlossaryUiV13( QString html )
{
  // sutraGlossaryContentUiV15
  // UI-only polish for the "Giáº£i nghÄ©a" tab.
  // Important: avoid global text replacement for "liÃªn quan" because it appears in the subtitle.
  // This function only styles structural headers and standalone labels.

  if ( html.contains( QStringLiteral( "data-sutra-glossary-v15" ) ) ) {
    return html;
  }

  html.remove( QRegularExpression( QStringLiteral( R"GD(<style[\s\S]*?</style>)GD" ) ) );

  // Main title.
  html.replace( QRegularExpression( QStringLiteral( R"GD(<h1[^>]*>\s*([^<]+)\s*</h1>)GD" ) ),
                QStringLiteral( "<div style='margin:4px 0 14px 0;padding:0 0 8px 0;"
                                "border-bottom:1px solid #e2e8f0;font-size:31px;line-height:1.22;"
                                "font-weight:800;color:#0f172a;'>\\1</div>" ) );

  // Section headers. First h2 is main result, all later h2 are related sections.
  QRegularExpression h2Regex( QStringLiteral( R"GD(<h2[^>]*>\s*([^<]+)\s*</h2>)GD" ) );
  QRegularExpressionMatchIterator h2It = h2Regex.globalMatch( html );

  QString rebuilt;
  int cursor = 0;
  int sectionIndex = 0;

  while ( h2It.hasNext() ) {
    const QRegularExpressionMatch match = h2It.next();

    rebuilt += html.mid( cursor, match.capturedStart() - cursor );

    if ( sectionIndex == 0 ) {
      rebuilt += QStringLiteral(
        "<div style='margin:18px 0 11px 0;padding:9px 12px;"
        "background:#ecfdf5;border-left:5px solid #0f766e;"
        "font-size:18px;line-height:1.2;font-weight:800;letter-spacing:.4px;"
        "color:#0f766e;'>K&#7870;T QU&#7842; CH&#205;NH</div>" );
    }
    else {
      rebuilt += QStringLiteral(
        "<div style='margin:22px 0 11px 0;padding:9px 12px;"
        "background:#fff7ed;border-left:5px solid #c2410c;"
        "font-size:18px;line-height:1.2;font-weight:800;letter-spacing:.4px;"
        "color:#9a3412;'>T&#7914; LI&#202;N QUAN</div>" );
    }

    cursor = match.capturedEnd();
    ++sectionIndex;
  }

  if ( cursor > 0 ) {
    rebuilt += html.mid( cursor );
    html = rebuilt;
  }

  // Entry term title.
  html.replace( QRegularExpression( QStringLiteral( R"GD(<h3[^>]*>)GD" ) ),
                QStringLiteral( "<h3 style='margin:12px 0 7px 0;font-size:24px;line-height:1.18;"
                                "font-weight:800;color:#020617;'>" ) );

  // Standalone labels. Use ASCII-safe output labels with HTML entities.
  const QString pinyinLabel = QStringLiteral(
    "<br><span style='display:inline-block;margin:14px 0 5px 0;"
    "font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>PINYIN</span><br>" );

  const QString meaningLabel = QStringLiteral(
    "<br><span style='display:inline-block;margin:14px 0 5px 0;"
    "font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>NGH&#296;A TI&#7870;NG VI&#7878;T</span><br>" );

  const QString suggestionLabel = QStringLiteral(
    "<br><span style='display:inline-block;margin:14px 0 5px 0;"
    "font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>G&#7906;I &#221; D&#7882;CH</span><br>" );

  const QString relatedLabel = QStringLiteral(
    "<br><span style='display:inline-block;margin:14px 0 5px 0;"
    "font-size:12px;font-weight:800;letter-spacing:1px;color:#64748b;'>LI&#202;N QUAN</span><br>" );

  html.replace( QRegularExpression( QStringLiteral( R"GD(\bPINYIN\b)GD" ) ),
                pinyinLabel );

  // Case-sensitive and label-shaped only. This prevents matching the subtitle phrase "tá»« liÃªn quan".
  html.replace( QRegularExpression( QStringLiteral( R"GD(Ngh[^<\n\r]{0,24}Vi[^<\n\r]{0,12}t)GD" ) ),
                meaningLabel );

  html.replace( QRegularExpression( QStringLiteral( R"GD(G[^<\n\r]{0,14}i[^<\n\r]{0,14}d[^<\n\r]{0,14}ch)GD" ) ),
                suggestionLabel );

  html.replace( QRegularExpression( QStringLiteral( R"GD(\bLi[^<\n\r]{0,14}n quan\b)GD" ) ),
                relatedLabel );

  // Related links/chips only.
  html.replace( QRegularExpression( QStringLiteral( R"GD(<a\s+)GD" ) ),
                QStringLiteral( "<a style='display:inline-block;margin:4px 7px 4px 0;"
                                "padding:4px 9px;background:#fff7ed;border:1px solid #fed7aa;"
                                "color:#9a3412;text-decoration:none;font-weight:700;' " ) );

  html.prepend( QStringLiteral(
    "<div data-sutra-glossary-v15='1' style='padding:14px 16px 22px 16px;"
    "background:#f8fafc;color:#0f172a;font-size:16px;line-height:1.58;"
    "font-family:Segoe UI,Arial,sans-serif;'>" ) );

  html.append( QStringLiteral( "</div>" ) );

  return html;
}



void updateBuddhistGlossaryTab( QTabWidget * tabs, const QString & primaryTerm, const QStringList & detectedTerms )
{
  if ( !tabs ) {
    return;
  }

  if ( !loadSutraPopupShowGlossaryTab() ) {
    removeBuddhistGlossaryTab( tabs );
    return;
  }

  QTextBrowser * browser = findBuddhistGlossaryBrowser( tabs );
  if ( !browser ) {
    browser = new QTextBrowser( tabs );
    browser->setObjectName( QStringLiteral( "buddhistGlossaryBrowser" ) );
    browser->setOpenExternalLinks( true );
    tabs->addTab( browser, sutraGlossaryTabTitle() );
  }

  browser->setHtml( sutraApplyPopupThemeToHtml( glossaryHtmlStableV16( primaryTerm, detectedTerms ) ) );
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
    "body{font-family:'Segoe UI','Noto Sans','Arial',sans-serif;font-size:%1px;line-height:1.55;margin:0;padding:14px 14px 56px;background:#f6f8fb;color:#1f2937;}"
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

  if ( !loadSutraPopupShowWebTab() ) {
    removeWebReferenceTab( tabs );
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

  browser->setHtml( sutraApplyPopupThemeToHtml(
    sutraSafeWebReferenceV13( webReferenceHtml( primaryTerm, detectedTerms ) ) ) );
  applySutraPopupTextBrowserFont( browser );
}


static bool sutraPopupIsFitToResultsMode( QWidget * popup )
{
  if ( !popup ) {
    return false;
  }

  QStatusBar * status = popup->findChild< QStatusBar * >();

  if ( !status ) {
    return false;
  }

  const QString message = status->currentMessage();

  return message.contains( QStringLiteral( "fit to results" ), Qt::CaseInsensitive )
      || ( message.contains( QStringLiteral( "fit" ), Qt::CaseInsensitive )
        && message.contains( QStringLiteral( "result" ), Qt::CaseInsensitive ) );
}

static void sutraApplyFitToResultsPopup( QWidget * popup, QTabWidget * tabs )
{
  if ( !popup || !tabs || !sutraPopupIsFitToResultsMode( popup ) ) {
    return;
  }

  QWidget * currentWidget = tabs->currentWidget();

  int contentWidth = 660;
  int contentHeight = 320;

  QTextBrowser * browser = currentWidget ? currentWidget->findChild< QTextBrowser * >() : nullptr;

  if ( !browser ) {
    browser = qobject_cast< QTextBrowser * >( currentWidget );
  }

  if ( browser && browser->document() ) {
    QTextDocument * document = browser->document();

    const qreal previousTextWidth = document->textWidth();
    const int preferredTextWidth = 720;

    document->setTextWidth( preferredTextWidth );
    document->adjustSize();

    const QSizeF documentSize = document->size();

    contentWidth = qBound( 420, static_cast< int >( document->idealWidth() ) + 40, 780 );
    contentHeight = qBound( 180, static_cast< int >( documentSize.height() ) + 40, 560 );

    if ( previousTextWidth > 0 ) {
      document->setTextWidth( previousTextWidth );
    }
  }
  else if ( currentWidget ) {
    const QSize hint = currentWidget->sizeHint();

    if ( hint.isValid() ) {
      contentWidth = qBound( 420, hint.width() + 40, 780 );
      contentHeight = qBound( 180, hint.height() + 40, 560 );
    }
  }

  const QRect available = popup->screen()
                            ? popup->screen()->availableGeometry()
                            : QApplication::primaryScreen()->availableGeometry();

  const int chromeWidth = 80;
  const int chromeHeight = 170;

  const int targetWidth = qBound( 520, contentWidth + chromeWidth, qMin( 920, available.width() - 60 ) );
  const int targetHeight = qBound( 300, contentHeight + chromeHeight, qMin( 760, available.height() - 60 ) );

  popup->resize( targetWidth, targetHeight );

  QPoint position = popup->pos();

  if ( position.x() + targetWidth > available.right() - 12 ) {
    position.setX( available.right() - targetWidth - 12 );
  }

  if ( position.y() + targetHeight > available.bottom() - 12 ) {
    position.setY( available.bottom() - targetHeight - 12 );
  }

  if ( position.x() < available.left() + 12 ) {
    position.setX( available.left() + 12 );
  }

  if ( position.y() < available.top() + 12 ) {
    position.setY( available.top() + 12 );
  }

  popup->move( position );
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

// Popup productivity suite --------------------------------------------------
// These features are intentionally self-contained in scanpopup.cc and use
// QSettings. They do not modify dictionary lookup, smart phrase detection,
// ArticleView navigation, or the existing GoldenDict history/favorites paths.

constexpr int sutraLookupHistoryLimit = 100;

QString sutraLookupHistorySettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupLookupHistory" );
}

QString sutraLookupFavoritesSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupLookupFavorites" );
}

QString sutraLookupNotesSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupLookupNotes" );
}

QString sutraLookupTimesSettingsKey()
{
  return QStringLiteral( "SutraEdition/PopupLookupTimes" );
}

QString sutraHistoryBackButtonObjectName()
{
  return QStringLiteral( "sutraHistoryBackButton" );
}

QString sutraHistoryForwardButtonObjectName()
{
  return QStringLiteral( "sutraHistoryForwardButton" );
}

QString sutraFavoriteButtonObjectName()
{
  return QStringLiteral( "sutraFavoriteButton" );
}

QString sutraNoteButtonObjectName()
{
  return QStringLiteral( "sutraNoteButton" );
}

QString sutraHistoryButtonObjectName()
{
  return QStringLiteral( "sutraHistoryButton" );
}

QStringList loadSutraLookupHistory()
{
  QSettings settings;
  QStringList history = settings.value( sutraLookupHistorySettingsKey() ).toStringList();
  history.removeAll( QString() );
  while ( history.size() > sutraLookupHistoryLimit ) {
    history.removeFirst();
  }
  return history;
}

void saveSutraLookupHistory( const QStringList & history )
{
  QSettings settings;
  settings.setValue( sutraLookupHistorySettingsKey(), history );
}

QStringList loadSutraLookupFavorites()
{
  QSettings settings;
  QStringList favorites = settings.value( sutraLookupFavoritesSettingsKey() ).toStringList();
  favorites.removeAll( QString() );
  favorites.removeDuplicates();
  return favorites;
}

void saveSutraLookupFavorites( const QStringList & favorites )
{
  QSettings settings;
  settings.setValue( sutraLookupFavoritesSettingsKey(), favorites );
}

QVariantMap loadSutraLookupNotes()
{
  QSettings settings;
  return settings.value( sutraLookupNotesSettingsKey() ).toMap();
}

void saveSutraLookupNotes( const QVariantMap & notes )
{
  QSettings settings;
  settings.setValue( sutraLookupNotesSettingsKey(), notes );
}

QVariantMap loadSutraLookupTimes()
{
  QSettings settings;
  return settings.value( sutraLookupTimesSettingsKey() ).toMap();
}

void saveSutraLookupTimes( const QVariantMap & times )
{
  QSettings settings;
  settings.setValue( sutraLookupTimesSettingsKey(), times );
}

QString sutraCurrentLookupTerm( QWidget * popup )
{
  if ( !popup ) {
    return {};
  }
  return normalizeSmartLookupInput( popup->property( "sutraCurrentLookupTerm" ).toString() );
}

QString sutraNoteForTerm( const QString & term )
{
  return loadSutraLookupNotes().value( term ).toString();
}

struct SutraLookupDetails
{
  QString original;
  QString hanViet;
  QString pinyin;
  QString meaning;
  QString suggested;
  QString note;
};

QList< const BuddhistGlossaryEntry * > sutraGlossaryEntriesForLookup( const QString & lookupText )
{
  QList< const BuddhistGlossaryEntry * > matches;
  const QString normalized = normalizeSmartLookupInput( lookupText );
  if ( normalized.isEmpty() ) {
    return matches;
  }

  const auto & entries = buddhistGlossaryEntries();
  for ( const BuddhistGlossaryEntry & entry : entries ) {
    if ( entry.term == normalized || entry.hanViet.compare( normalized, Qt::CaseInsensitive ) == 0
         || entry.suggestedTranslations.contains( normalized, Qt::CaseInsensitive ) ) {
      matches << &entry;
      return matches;
    }
  }

  const QStringList detected = detectSmartLookupTerms( normalized );
  for ( const QString & detectedTerm : detected ) {
    for ( const BuddhistGlossaryEntry & entry : entries ) {
      if ( entry.term == detectedTerm && !matches.contains( &entry ) ) {
        matches << &entry;
        break;
      }
    }
  }

  return matches;
}

SutraLookupDetails sutraLookupDetailsForTerm( const QString & lookupText )
{
  SutraLookupDetails details;
  details.original = normalizeSmartLookupInput( lookupText );
  details.note     = sutraNoteForTerm( details.original );

  QStringList hanVietValues;
  QStringList pinyinValues;
  QStringList meaningValues;
  QStringList suggestedValues;

  const auto matches = sutraGlossaryEntriesForLookup( details.original );
  for ( const BuddhistGlossaryEntry * entry : matches ) {
    if ( !entry ) {
      continue;
    }
    if ( !entry->hanViet.isEmpty() && !hanVietValues.contains( entry->hanViet ) ) {
      hanVietValues << entry->hanViet;
    }
    if ( !entry->pinyin.isEmpty() && !pinyinValues.contains( entry->pinyin ) ) {
      pinyinValues << entry->pinyin;
    }
    if ( !entry->meaningVi.isEmpty() && !meaningValues.contains( entry->meaningVi ) ) {
      meaningValues << entry->meaningVi;
    }
    for ( const QString & value : entry->suggestedTranslations ) {
      if ( !value.isEmpty() && !suggestedValues.contains( value ) ) {
        suggestedValues << value;
      }
    }
  }

  details.hanViet   = hanVietValues.join( QStringLiteral( " | " ) );
  details.pinyin    = pinyinValues.join( QStringLiteral( " | " ) );
  details.meaning   = meaningValues.join( QStringLiteral( "\n" ) );
  details.suggested = suggestedValues.join( QStringLiteral( " | " ) );
  return details;
}

QString sutraFullCopyText( const SutraLookupDetails & details )
{
  QStringList lines;
  if ( !details.original.isEmpty() ) {
    lines << QStringLiteral( "Original: %1" ).arg( details.original );
  }
  if ( !details.hanViet.isEmpty() ) {
    lines << QStringLiteral( "Han-Viet: %1" ).arg( details.hanViet );
  }
  if ( !details.pinyin.isEmpty() ) {
    lines << QStringLiteral( "Pinyin: %1" ).arg( details.pinyin );
  }
  if ( !details.meaning.isEmpty() ) {
    lines << QStringLiteral( "Meaning: %1" ).arg( details.meaning );
  }
  if ( !details.suggested.isEmpty() ) {
    lines << QStringLiteral( "Suggested translation: %1" ).arg( details.suggested );
  }
  if ( !details.note.isEmpty() ) {
    lines << QStringLiteral( "Note: %1" ).arg( details.note );
  }
  return lines.join( QLatin1Char( '\n' ) );
}

void copySutraTextToClipboard( const QString & text )
{
  if ( text.isEmpty() ) {
    return;
  }
  if ( QClipboard * clipboard = QApplication::clipboard() ) {
    clipboard->setText( text, QClipboard::Clipboard );
  }
}

void updateSutraLookupFeatureControls( QWidget * popup, const QString & term = QString() )
{
  if ( !popup ) {
    return;
  }

  const QString currentTerm = term.isEmpty() ? sutraCurrentLookupTerm( popup ) : normalizeSmartLookupInput( term );
  const QStringList history = loadSutraLookupHistory();
  int historyIndex          = popup->property( "sutraLookupHistoryIndex" ).toInt();
  if ( history.isEmpty() ) {
    historyIndex = -1;
  }
  else if ( historyIndex < 0 || historyIndex >= history.size() ) {
    historyIndex = history.size() - 1;
  }
  popup->setProperty( "sutraLookupHistoryIndex", historyIndex );

  if ( QToolButton * back = popup->findChild< QToolButton * >( sutraHistoryBackButtonObjectName() ) ) {
    back->setEnabled( historyIndex > 0 );
    back->setIcon( sutraPopupNavigationIcon( false, sutraPopupToolbarIconColor( back->isEnabled() ) ) );
  }
  if ( QToolButton * forward = popup->findChild< QToolButton * >( sutraHistoryForwardButtonObjectName() ) ) {
    forward->setEnabled( historyIndex >= 0 && historyIndex + 1 < history.size() );
    forward->setIcon( sutraPopupNavigationIcon( true, sutraPopupToolbarIconColor( forward->isEnabled() ) ) );
  }
  if ( QToolButton * historyButton = popup->findChild< QToolButton * >( sutraHistoryButtonObjectName() ) ) {
    const QString label = QObject::tr( "Lookup history and favorites (%1)" ).arg( history.size() );
    historyButton->setToolTip( label );
    historyButton->setAccessibleName( label );
    historyButton->setText( QString() );
    historyButton->setIcon( sutraPopupHistoryIcon( sutraPopupActiveTextColor() ) );
    historyButton->setIconSize( QSize( 18, 18 ) );
  }

  const bool favorite = !currentTerm.isEmpty() && loadSutraLookupFavorites().contains( currentTerm );
  if ( QToolButton * favoriteButton = popup->findChild< QToolButton * >( sutraFavoriteButtonObjectName() ) ) {
    favoriteButton->setText( QString( QChar( favorite ? 0x2605 : 0x2606 ) ) );
    favoriteButton->setToolTip( favorite ? QObject::tr( "Remove from popup favorites" )
                                         : QObject::tr( "Add to popup favorites" ) );
    favoriteButton->setEnabled( !currentTerm.isEmpty() );
  }

  if ( QToolButton * noteButton = popup->findChild< QToolButton * >( sutraNoteButtonObjectName() ) ) {
    const bool hasNote = !currentTerm.isEmpty() && !sutraNoteForTerm( currentTerm ).isEmpty();
    noteButton->setEnabled( !currentTerm.isEmpty() );
    noteButton->setText( QString() );
    noteButton->setIcon( sutraPopupNoteIcon( sutraPopupToolbarIconColor( noteButton->isEnabled() ), hasNote ) );
    noteButton->setIconSize( QSize( 18, 18 ) );
    noteButton->setToolTip( hasNote ? QObject::tr( "Edit personal note" ) : QObject::tr( "Add personal note" ) );
    noteButton->setAccessibleName( hasNote ? QObject::tr( "Edit personal note" )
                                           : QObject::tr( "Add personal note" ) );
  }
}

void recordSutraLookupHistory( QWidget * popup, const QString & lookupText )
{
  if ( !popup ) {
    return;
  }

  const QString term = normalizeSmartLookupInput( lookupText );
  if ( term.isEmpty() ) {
    return;
  }

  popup->setProperty( "sutraCurrentLookupTerm", term );

  const bool navigating = popup->property( "sutraLookupHistoryNavigating" ).toBool();
  popup->setProperty( "sutraLookupHistoryNavigating", false );

  QStringList history = loadSutraLookupHistory();
  if ( navigating ) {
    int index = popup->property( "sutraLookupHistoryIndex" ).toInt();
    const int matchingIndex = history.lastIndexOf( term );
    if ( matchingIndex >= 0 ) {
      index = matchingIndex;
    }
    popup->setProperty( "sutraLookupHistoryIndex", index );
  }
  else {
    int index = popup->property( "sutraLookupHistoryIndex" ).toInt();
    if ( index >= 0 && index + 1 < history.size() ) {
      history = history.mid( 0, index + 1 );
    }

    if ( history.isEmpty() || history.constLast() != term ) {
      history << term;
      while ( history.size() > sutraLookupHistoryLimit ) {
        history.removeFirst();
      }
      saveSutraLookupHistory( history );
    }
    popup->setProperty( "sutraLookupHistoryIndex", history.size() - 1 );
  }

  QVariantMap times = loadSutraLookupTimes();
  times.insert( term, QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
  saveSutraLookupTimes( times );

  updateSutraLookupFeatureControls( popup, term );
}

QString sutraCsvCell( QString value )
{
  value.replace( QLatin1Char( '"' ), QStringLiteral( "\"\"" ) );
  return QStringLiteral( "\"%1\"" ).arg( value );
}

QStringList sutraExportTerms()
{
  const QStringList history = loadSutraLookupHistory();
  QStringList terms;
  for ( int i = history.size() - 1; i >= 0; --i ) {
    const QString & term = history.at( i );
    if ( !terms.contains( term ) ) {
      terms << term;
    }
  }
  for ( const QString & favorite : loadSutraLookupFavorites() ) {
    if ( !terms.contains( favorite ) ) {
      terms << favorite;
    }
  }
  return terms;
}

bool exportSutraLookupDataCsv( QWidget * parent )
{
  const QString path = QFileDialog::getSaveFileName( parent,
                                                     QObject::tr( "Export popup data as CSV" ),
                                                     QStringLiteral( "sutra-popup-data.csv" ),
                                                     QObject::tr( "CSV files (*.csv)" ) );
  if ( path.isEmpty() ) {
    return false;
  }

  const QStringList favorites = loadSutraLookupFavorites();
  const QVariantMap times     = loadSutraLookupTimes();
  QStringList lines;
  lines << QStringLiteral( "\"term\",\"han_viet\",\"pinyin\",\"meaning_vi\",\"suggested_translation\",\"note\",\"favorite\",\"last_lookup_utc\"" );

  for ( const QString & term : sutraExportTerms() ) {
    const SutraLookupDetails details = sutraLookupDetailsForTerm( term );
    lines << QStringList{ sutraCsvCell( term ),
                          sutraCsvCell( details.hanViet ),
                          sutraCsvCell( details.pinyin ),
                          sutraCsvCell( details.meaning ),
                          sutraCsvCell( details.suggested ),
                          sutraCsvCell( details.note ),
                          sutraCsvCell( favorites.contains( term ) ? QStringLiteral( "true" ) : QStringLiteral( "false" ) ),
                          sutraCsvCell( times.value( term ).toString() ) }
               .join( QLatin1Char( ',' ) );
  }

  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
    QMessageBox::warning( parent, QObject::tr( "Export failed" ), file.errorString() );
    return false;
  }

  file.write( QByteArray::fromHex( "EFBBBF" ) );
  file.write( lines.join( QStringLiteral( "\r\n" ) ).toUtf8() );
  file.close();
  return true;
}

bool exportSutraLookupDataJson( QWidget * parent )
{
  const QString path = QFileDialog::getSaveFileName( parent,
                                                     QObject::tr( "Export popup data as JSON" ),
                                                     QStringLiteral( "sutra-popup-data.json" ),
                                                     QObject::tr( "JSON files (*.json)" ) );
  if ( path.isEmpty() ) {
    return false;
  }

  const QStringList favorites = loadSutraLookupFavorites();
  const QVariantMap times     = loadSutraLookupTimes();
  QJsonArray array;
  for ( const QString & term : sutraExportTerms() ) {
    const SutraLookupDetails details = sutraLookupDetailsForTerm( term );
    QJsonObject object;
    object.insert( QStringLiteral( "term" ), term );
    object.insert( QStringLiteral( "han_viet" ), details.hanViet );
    object.insert( QStringLiteral( "pinyin" ), details.pinyin );
    object.insert( QStringLiteral( "meaning_vi" ), details.meaning );
    object.insert( QStringLiteral( "suggested_translation" ), details.suggested );
    object.insert( QStringLiteral( "note" ), details.note );
    object.insert( QStringLiteral( "favorite" ), favorites.contains( term ) );
    object.insert( QStringLiteral( "last_lookup_utc" ), times.value( term ).toString() );
    array.append( object );
  }

  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
    QMessageBox::warning( parent, QObject::tr( "Export failed" ), file.errorString() );
    return false;
  }
  file.write( QJsonDocument( array ).toJson( QJsonDocument::Indented ) );
  file.close();
  return true;
}

} // namespace

#ifdef Q_OS_MAC
  #include "macos/macmouseover.hh"
  #define MouseOver MacMouseOver
#endif

static const Qt::WindowFlags defaultUnpinnedWindowFlags = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;

static const Qt::WindowFlags pinnedWindowFlags = Qt::Window;


static void sutraRestorePopupFromMinimized( QWidget * popup )
{
  if ( !popup ) {
    return;
  }

  const Qt::WindowStates states = popup->windowState();

  if ( states.testFlag( Qt::WindowMinimized ) ) {
    popup->setWindowState( ( states & ~Qt::WindowMinimized ) | Qt::WindowActive );
    popup->showNormal();
  }
  else {
    popup->setWindowState( states | Qt::WindowActive );
    popup->show();
  }

  popup->raise();
  popup->activateWindow();
}
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
  tabWidget->tabBar()->setExpanding( false );
  tabWidget->tabBar()->setUsesScrollButtons( true );
  tabWidget->tabBar()->setElideMode( Qt::ElideRight );
  tabWidget->tabBar()->setMovable( false );
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
  definition->setProperty( "sutraPopupPrimaryArticleTab", true );

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
  // This button now opens the popup settings menu. A gear avoids the old red
  // pin icon suggesting that clicking it directly toggles pinning.
  ui.pinButton->setText( QString() );
  ui.pinButton->setIcon( sutraPopupSettingsIcon( sutraPopupActiveTextColor() ) );
  ui.pinButton->setIconSize( QSize( 20, 20 ) );
  ui.pinButton->setToolTip( tr( "Popup settings" ) );
  ui.pinButton->setAccessibleName( tr( "Popup settings" ) );

  QMenu * sutraPopupLayoutMenu = new QMenu( tr( "Popup options" ), ui.pinButton );

  QAction * sutraPopupPinAction = sutraPopupLayoutMenu->addAction( tr( "Pin / keep popup open" ) );
  sutraPopupPinAction->setCheckable( true );

  QAction * sutraPopupAlwaysOnTopAction = sutraPopupLayoutMenu->addAction( tr( "Always stay on top" ) );
  sutraPopupAlwaysOnTopAction->setCheckable( true );
  sutraPopupAlwaysOnTopAction->setToolTip( tr( "Keep a pinned popup above other windows" ) );

  sutraPopupLayoutMenu->addSeparator();

  QActionGroup * sutraPopupLayoutGroup = new QActionGroup( sutraPopupLayoutMenu );

  QAction * sutraPopupAutoAction  = sutraPopupLayoutMenu->addAction( tr( "Auto - let GoldenDict decide" ) );
  QAction * sutraPopupFixedAction = sutraPopupLayoutMenu->addAction( tr( "Fix current size and position" ) );
  QAction * sutraPopupFitAction   = sutraPopupLayoutMenu->addAction( tr( "Fit window size to results" ) );

  sutraPopupLayoutGroup->setExclusive( true );
  for ( QAction * action : { sutraPopupAutoAction, sutraPopupFixedAction, sutraPopupFitAction } ) {
    action->setCheckable( true );
    sutraPopupLayoutGroup->addAction( action );
  }

  const auto syncSutraPopupLayoutActions = [ sutraPopupLayoutGroup,
                                              sutraPopupAutoAction,
                                              sutraPopupFixedAction,
                                              sutraPopupFitAction ]( SutraPopupLayoutMode mode ) {
    // Clear the old mark first. This avoids a stale checked action when the
    // selected layout mode is changed programmatically or through QSettings.
    sutraPopupLayoutGroup->setExclusive( false );
    sutraPopupAutoAction->setChecked( mode == SutraPopupLayoutMode::Auto );
    sutraPopupFixedAction->setChecked( mode == SutraPopupLayoutMode::Fixed );
    sutraPopupFitAction->setChecked( mode == SutraPopupLayoutMode::FitToResults );
    sutraPopupLayoutGroup->setExclusive( true );
  };

  sutraPopupLayoutMenu->addSeparator();

  QMenu * sutraPopupTabsMenu = sutraPopupLayoutMenu->addMenu( tr( "Tabs" ) );

  QAction * sutraPopupPrimaryTabInfoAction =
    sutraPopupTabsMenu->addAction( tr( "Dictionary tab (always shown)" ) );
  sutraPopupPrimaryTabInfoAction->setEnabled( false );

  sutraPopupTabsMenu->addSeparator();

  QAction * sutraPopupShowGlossaryTabAction =
    sutraPopupTabsMenu->addAction( tr( "Show %1 tab" ).arg( sutraGlossaryTabTitle() ) );
  sutraPopupShowGlossaryTabAction->setCheckable( true );
  sutraPopupShowGlossaryTabAction->setToolTip(
    tr( "Show or hide the Buddhist glossary explanation tab" ) );

  QAction * sutraPopupShowWebTabAction = sutraPopupTabsMenu->addAction( tr( "Show Web tab" ) );
  sutraPopupShowWebTabAction->setCheckable( true );
  sutraPopupShowWebTabAction->setToolTip(
    tr( "Show or hide external web reference links" ) );

  sutraPopupLayoutMenu->addSeparator();

  const auto applySutraPopupZoom = [ this ]( int requestedFontSize ) {
    const int fontSize = qBound( sutraPopupMinFontSize, requestedFontSize, sutraPopupMaxFontSize );

    if ( fontSize == loadSutraPopupFontSize() ) {
      updateSutraPopupZoomIndicator( this );
      return;
    }

    saveSutraPopupFontSize( fontSize );
    applyZoomFactor();

    // Re-render only the popup presentation so inline HTML sizes follow the zoom level.
    // The current lookup text and translation result remain unchanged.
    refreshSutraCustomTabs( tabWidget, pendingWord, translateBox->translateLine()->text() );
    applySutraPopupFontSizeToTabs( tabWidget );
    updateSutraPopupZoomIndicator( this );

    if ( loadSutraPopupLayoutMode() == SutraPopupLayoutMode::FitToResults ) {
      QTimer::singleShot( 0, this, [ this ] {
        fitSutraPopupToResults( this, tabWidget );
        positionSutraPopupCornerTools( this );
      } );
    }

    showStatusBarMessage( tr( "Popup zoom: %1%" ).arg( sutraPopupZoomPercent() ), 3000 );
  };

  QMenu * sutraPopupFontSizeMenu         = sutraPopupLayoutMenu->addMenu( tr( "Zoom" ) );
  QActionGroup * sutraPopupFontSizeGroup = new QActionGroup( sutraPopupFontSizeMenu );
  QList< QAction * > sutraPopupFontSizeActions;

  QAction * sutraPopupZoomOutAction = new QAction( tr( "Zoom out" ), this );
  sutraPopupZoomOutAction->setObjectName( QStringLiteral( "sutraPopupZoomOutAction" ) );
  sutraPopupZoomOutAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  sutraPopupZoomOutAction->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_Minus ) );
  addAction( sutraPopupZoomOutAction );
  sutraPopupFontSizeMenu->addAction( sutraPopupZoomOutAction );

  QAction * sutraPopupZoomResetAction = new QAction( tr( "Reset zoom (100%)" ), this );
  sutraPopupZoomResetAction->setObjectName( QStringLiteral( "sutraPopupZoomResetAction" ) );
  sutraPopupZoomResetAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  sutraPopupZoomResetAction->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_0 ) );
  addAction( sutraPopupZoomResetAction );
  sutraPopupFontSizeMenu->addAction( sutraPopupZoomResetAction );

  QAction * sutraPopupZoomInAction = new QAction( tr( "Zoom in" ), this );
  sutraPopupZoomInAction->setObjectName( QStringLiteral( "sutraPopupZoomInAction" ) );
  sutraPopupZoomInAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  sutraPopupZoomInAction->setShortcuts(
    QList< QKeySequence >() << QKeySequence( Qt::CTRL | Qt::Key_Plus )
                            << QKeySequence( Qt::CTRL | Qt::Key_Equal ) );
  addAction( sutraPopupZoomInAction );
  sutraPopupFontSizeMenu->addAction( sutraPopupZoomInAction );

  connect( sutraPopupZoomOutAction, &QAction::triggered, this, [ applySutraPopupZoom ] {
    applySutraPopupZoom( nextSutraPopupFontSize( -1 ) );
  } );
  connect( sutraPopupZoomResetAction, &QAction::triggered, this, [ applySutraPopupZoom ] {
    applySutraPopupZoom( sutraPopupDefaultFontSize );
  } );
  connect( sutraPopupZoomInAction, &QAction::triggered, this, [ applySutraPopupZoom ] {
    applySutraPopupZoom( nextSutraPopupFontSize( 1 ) );
  } );

  sutraPopupFontSizeMenu->addSeparator();

  for ( int fontSize : { 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 32, 36 } ) {
    QAction * fontAction = sutraPopupFontSizeMenu->addAction( tr( "%1 px" ).arg( fontSize ) );
    fontAction->setCheckable( true );
    fontAction->setData( fontSize );
    sutraPopupFontSizeGroup->addAction( fontAction );
    sutraPopupFontSizeActions << fontAction;

    connect( fontAction, &QAction::triggered, this, [ applySutraPopupZoom, fontSize ] {
      applySutraPopupZoom( fontSize );
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

  const auto applySutraPopupThemeMode = [ this ]( SutraPopupThemeMode mode ) {
    QPointer< QWidget > previousCurrentTab = tabWidget ? tabWidget->currentWidget() : nullptr;

    saveSutraPopupThemeMode( mode );
    applySutraPopupTheme( this, tabWidget );

    // Re-render only the custom presentation HTML. Lookup detection, selected word,
    // glossary data and translation logic are not changed.
    refreshSutraCustomTabs( tabWidget, pendingWord, translateBox->translateLine()->text() );
    applySutraPopupFontSizeToTabs( tabWidget );
    applySutraPopupTheme( this, tabWidget );

    if ( previousCurrentTab && tabWidget && tabWidget->indexOf( previousCurrentTab ) >= 0 ) {
      tabWidget->setCurrentWidget( previousCurrentTab );
    }

    if ( loadSutraPopupLayoutMode() == SutraPopupLayoutMode::FitToResults ) {
      QTimer::singleShot( 0, this, [ this ] {
        fitSutraPopupToResults( this, tabWidget );
        positionSutraPopupCornerTools( this );
      } );
    }

    showStatusBarMessage( tr( "Popup theme: %1" ).arg( sutraPopupThemeModeLabel( mode ) ), 3500 );
  };

  QMenu * sutraPopupThemeMenu         = sutraPopupLayoutMenu->addMenu( tr( "Theme" ) );
  QActionGroup * sutraPopupThemeGroup = new QActionGroup( sutraPopupThemeMenu );
  QList< QAction * > sutraPopupThemeActions;

  QAction * sutraPopupLightThemeAction = sutraPopupThemeMenu->addAction( tr( "Light" ) );
  sutraPopupLightThemeAction->setCheckable( true );
  sutraPopupLightThemeAction->setData( static_cast< int >( SutraPopupThemeMode::Light ) );
  sutraPopupThemeGroup->addAction( sutraPopupLightThemeAction );
  sutraPopupThemeActions << sutraPopupLightThemeAction;

  QAction * sutraPopupDarkThemeAction = sutraPopupThemeMenu->addAction( tr( "Dark" ) );
  sutraPopupDarkThemeAction->setCheckable( true );
  sutraPopupDarkThemeAction->setData( static_cast< int >( SutraPopupThemeMode::Dark ) );
  sutraPopupThemeGroup->addAction( sutraPopupDarkThemeAction );
  sutraPopupThemeActions << sutraPopupDarkThemeAction;

  QAction * sutraPopupCustomThemeAction = sutraPopupThemeMenu->addAction( tr( "Custom colors" ) );
  sutraPopupCustomThemeAction->setCheckable( true );
  sutraPopupCustomThemeAction->setData( static_cast< int >( SutraPopupThemeMode::Custom ) );
  sutraPopupThemeGroup->addAction( sutraPopupCustomThemeAction );
  sutraPopupThemeActions << sutraPopupCustomThemeAction;

  connect( sutraPopupLightThemeAction, &QAction::triggered, this, [ applySutraPopupThemeMode ] {
    applySutraPopupThemeMode( SutraPopupThemeMode::Light );
  } );
  connect( sutraPopupDarkThemeAction, &QAction::triggered, this, [ applySutraPopupThemeMode ] {
    applySutraPopupThemeMode( SutraPopupThemeMode::Dark );
  } );
  connect( sutraPopupCustomThemeAction, &QAction::triggered, this, [ applySutraPopupThemeMode ] {
    applySutraPopupThemeMode( SutraPopupThemeMode::Custom );
  } );

  sutraPopupThemeMenu->addSeparator();

  QAction * sutraPopupThemeToggleAction = new QAction( tr( "Toggle Light / Dark" ), this );
  sutraPopupThemeToggleAction->setObjectName( QStringLiteral( "sutraPopupThemeToggleAction" ) );
  sutraPopupThemeToggleAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  sutraPopupThemeToggleAction->setShortcut( QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_L ) );
  addAction( sutraPopupThemeToggleAction );
  sutraPopupThemeMenu->addAction( sutraPopupThemeToggleAction );

  connect( sutraPopupThemeToggleAction, &QAction::triggered, this, [ applySutraPopupThemeMode ] {
    const SutraPopupThemeMode currentMode = loadSutraPopupThemeMode();
    const SutraPopupThemeMode nextMode = currentMode == SutraPopupThemeMode::Dark
                                               ? SutraPopupThemeMode::Light
                                               : ( currentMode == SutraPopupThemeMode::Custom
                                                     ? SutraPopupThemeMode::Light
                                                     : SutraPopupThemeMode::Dark );
    applySutraPopupThemeMode( nextMode );
  } );

  QMenu * sutraPopupColorsMenu = sutraPopupThemeMenu->addMenu( tr( "Colors" ) );

  QAction * sutraPopupTextColorAction = sutraPopupColorsMenu->addAction( tr( "Text color..." ) );
  QAction * sutraPopupBackgroundColorAction = sutraPopupColorsMenu->addAction( tr( "Popup background..." ) );
  sutraPopupColorsMenu->addSeparator();
  QAction * sutraPopupResetColorsAction = sutraPopupColorsMenu->addAction( tr( "Reset custom colors" ) );

  const auto chooseSutraPopupColor = [ this, applySutraPopupThemeMode ]( bool chooseTextColor ) {
    const QColor initialColor = chooseTextColor ? loadSutraPopupCustomTextColor()
                                                : loadSutraPopupCustomBackgroundColor();
    const QString title = chooseTextColor ? tr( "Choose popup text color" )
                                          : tr( "Choose popup background color" );
    const QColor selectedColor = QColorDialog::getColor( initialColor, this, title );
    if ( !selectedColor.isValid() ) {
      return;
    }

    if ( chooseTextColor ) {
      saveSutraPopupCustomTextColor( selectedColor );
    }
    else {
      saveSutraPopupCustomBackgroundColor( selectedColor );
    }

    applySutraPopupThemeMode( SutraPopupThemeMode::Custom );
  };

  connect( sutraPopupTextColorAction, &QAction::triggered, this, [ chooseSutraPopupColor ] {
    chooseSutraPopupColor( true );
  } );
  connect( sutraPopupBackgroundColorAction, &QAction::triggered, this, [ chooseSutraPopupColor ] {
    chooseSutraPopupColor( false );
  } );
  connect( sutraPopupResetColorsAction, &QAction::triggered, this, [ applySutraPopupThemeMode ] {
    QSettings settings;
    settings.remove( sutraPopupCustomTextColorSettingsKey() );
    settings.remove( sutraPopupCustomBackgroundColorSettingsKey() );
    applySutraPopupThemeMode( SutraPopupThemeMode::Light );
  } );

  connect( sutraPopupColorsMenu, &QMenu::aboutToShow, this, [ sutraPopupTextColorAction,
                                                              sutraPopupBackgroundColorAction ] {
    sutraPopupTextColorAction->setIcon( sutraPopupColorSwatchIcon( loadSutraPopupCustomTextColor() ) );
    sutraPopupBackgroundColorAction->setIcon(
      sutraPopupColorSwatchIcon( loadSutraPopupCustomBackgroundColor() ) );
  } );

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

  sutraMouseLookupMenu->addSeparator();
  QMenu * sutraMouseCaptureMenu = sutraMouseLookupMenu->addMenu( tr( "Text capture" ) );
  QActionGroup * sutraMouseCaptureGroup = new QActionGroup( sutraMouseCaptureMenu );
  QList< QAction * > sutraMouseCaptureActions;

  const auto addSutraMouseCaptureAction = [ & ]( const QString & label, SutraMouseLookupCaptureMode mode ) {
    QAction * action = sutraMouseCaptureMenu->addAction( label );
    action->setCheckable( true );
    action->setData( static_cast< int >( mode ) );
    sutraMouseCaptureGroup->addAction( action );
    sutraMouseCaptureActions << action;

    connect( action, &QAction::triggered, this, [ this, mode ] {
      saveSutraMouseLookupCaptureMode( mode );
      showStatusBarMessage( tr( "Mouse text capture: %1" ).arg( sutraMouseLookupCaptureModeLabel( mode ) ), 5000 );
    } );
  };

  addSutraMouseCaptureAction( tr( "Automatic - detect precise phrase under pointer" ),
                              SutraMouseLookupCaptureMode::Automatic );
  addSutraMouseCaptureAction( tr( "Automatic - detect entire phrase under pointer" ),
                              SutraMouseLookupCaptureMode::EntirePhrase );
  addSutraMouseCaptureAction( tr( "Manual - use selected text only" ),
                              SutraMouseLookupCaptureMode::SelectedText );

  QAction * sutraMouseLookupInfoAction = sutraPopupLayoutMenu->addAction(
    tr( "Current mouse lookup: %1 / %2" )
      .arg( sutraMouseLookupModeLabel( loadSutraMouseLookupMode() ),
            sutraMouseLookupCaptureModeLabel( loadSutraMouseLookupCaptureMode() ) ) );
  sutraMouseLookupInfoAction->setEnabled( false );

  sutraPopupLayoutMenu->addSeparator();

  QAction * sutraPopupRestoreDefaultsAction = sutraPopupLayoutMenu->addAction( tr( "Restore popup defaults" ) );

  const auto updateSutraPopupLayoutMenu = [ this,
                                            sutraPopupPinAction,
                                            sutraPopupAlwaysOnTopAction,
                                            sutraPopupAutoAction,
                                            sutraPopupFixedAction,
                                            sutraPopupFitAction,
                                            syncSutraPopupLayoutActions,
                                            sutraPopupFontSizeActions,
                                            sutraPopupTransparencyActions,
                                            sutraPopupThemeActions,
                                            sutraPopupShowGlossaryTabAction,
                                            sutraPopupShowWebTabAction,
                                            sutraMouseLookupActions,
                                            sutraMouseCaptureActions,
                                            sutraMouseLookupInfoAction ] {
    sutraPopupPinAction->setChecked( ui.pinButton->isChecked() );
    sutraPopupAlwaysOnTopAction->setEnabled( ui.pinButton->isChecked() );
    sutraPopupAlwaysOnTopAction->setChecked( ui.onTopButton->isChecked() );

    const int currentFontSize = loadSutraPopupFontSize();
    for ( QAction * fontAction : sutraPopupFontSizeActions ) {
      fontAction->setChecked( fontAction->data().toInt() == currentFontSize );
    }

    const int currentOpacityPercent = loadSutraPopupOpacityPercent();
    for ( QAction * opacityAction : sutraPopupTransparencyActions ) {
      opacityAction->setChecked( opacityAction->data().toInt() == currentOpacityPercent );
    }

    const SutraPopupThemeMode currentThemeMode = loadSutraPopupThemeMode();
    for ( QAction * themeAction : sutraPopupThemeActions ) {
      themeAction->setChecked( themeAction->data().toInt() == static_cast< int >( currentThemeMode ) );
    }

    sutraPopupShowGlossaryTabAction->setChecked( loadSutraPopupShowGlossaryTab() );
    sutraPopupShowWebTabAction->setChecked( loadSutraPopupShowWebTab() );

    const SutraMouseLookupMode currentMouseLookupMode = loadSutraMouseLookupMode();
    for ( QAction * mouseLookupAction : sutraMouseLookupActions ) {
      mouseLookupAction->setChecked( mouseLookupAction->data().toInt()
                                     == static_cast< int >( currentMouseLookupMode ) );
    }

    const SutraMouseLookupCaptureMode currentCaptureMode = loadSutraMouseLookupCaptureMode();
    for ( QAction * captureAction : sutraMouseCaptureActions ) {
      captureAction->setChecked( captureAction->data().toInt()
                                 == static_cast< int >( currentCaptureMode ) );
    }

    sutraMouseLookupInfoAction->setText(
      tr( "Current mouse lookup: %1 / %2" )
        .arg( sutraMouseLookupModeLabel( currentMouseLookupMode ),
              sutraMouseLookupCaptureModeLabel( currentCaptureMode ) ) );

    syncSutraPopupLayoutActions( loadSutraPopupLayoutMode() );
  };

  connect( sutraPopupLayoutMenu, &QMenu::aboutToShow, this, updateSutraPopupLayoutMenu );

  connect( sutraPopupPinAction, &QAction::triggered, this, [ this ]( bool checked ) {
    ui.pinButton->setChecked( checked );
    pinButtonClicked( checked );
    showStatusBarMessage( checked ? tr( "Popup pinned" ) : tr( "Popup unpinned" ), 4000 );
  } );

  connect( sutraPopupAlwaysOnTopAction, &QAction::triggered, this, [ this ]( bool checked ) {
    ui.onTopButton->setChecked( checked );
    alwaysOnTopClicked( checked );
    showStatusBarMessage( checked ? tr( "Always stay on top: enabled" )
                                  : tr( "Always stay on top: disabled" ),
                          4000 );
  } );

  connect( sutraPopupAutoAction, &QAction::triggered, this, [ this, syncSutraPopupLayoutActions ] {
    saveSutraPopupLayoutMode( SutraPopupLayoutMode::Auto );
    syncSutraPopupLayoutActions( SutraPopupLayoutMode::Auto );
    showStatusBarMessage( tr( "Popup layout: Auto" ), 4000 );
  } );

  connect( sutraPopupFixedAction, &QAction::triggered, this, [ this, syncSutraPopupLayoutActions ] {
    saveSutraPopupFixedGeometry( this );
    syncSutraPopupLayoutActions( SutraPopupLayoutMode::Fixed );
    showStatusBarMessage( tr( "Popup layout: fixed current size and position" ), 5000 );
  } );

  connect( sutraPopupFitAction, &QAction::triggered, this, [ this, syncSutraPopupLayoutActions ] {
    saveSutraPopupLayoutMode( SutraPopupLayoutMode::FitToResults );
    syncSutraPopupLayoutActions( SutraPopupLayoutMode::FitToResults );
    fitSutraPopupToResults( this, tabWidget );
    showStatusBarMessage( tr( "Popup layout: fit to results" ), 4000 );
  } );

  connect( sutraPopupShowGlossaryTabAction, &QAction::triggered, this, [ this ]( bool checked ) {
    saveSutraPopupShowGlossaryTab( checked );
    refreshSutraCustomTabs( tabWidget, pendingWord, translateBox->translateLine()->text() );
    applySutraPopupTheme( this, tabWidget );
    applySutraPopupFontSizeToTabs( tabWidget );

    if ( checked ) {
      if ( QTextBrowser * browser = findBuddhistGlossaryBrowser( tabWidget ) ) {
        tabWidget->setCurrentWidget( browser );
      }
    }
    else if ( tabWidget->count() > 0 ) {
      tabWidget->setCurrentIndex( 0 );
    }

    if ( loadSutraPopupLayoutMode() == SutraPopupLayoutMode::FitToResults ) {
      fitSutraPopupToResults( this, tabWidget );
    }
    positionSutraPopupCornerTools( this );

    showStatusBarMessage(
      checked ? tr( "Definition tab shown" ) : tr( "Definition tab hidden" ),
      3500 );
  } );

  connect( sutraPopupShowWebTabAction, &QAction::triggered, this, [ this ]( bool checked ) {
    saveSutraPopupShowWebTab( checked );
    refreshSutraCustomTabs( tabWidget, pendingWord, translateBox->translateLine()->text() );
    applySutraPopupTheme( this, tabWidget );
    applySutraPopupFontSizeToTabs( tabWidget );

    if ( checked ) {
      if ( QTextBrowser * browser = findWebReferenceBrowser( tabWidget ) ) {
        tabWidget->setCurrentWidget( browser );
      }
    }
    else if ( tabWidget->count() > 0 ) {
      tabWidget->setCurrentIndex( 0 );
    }

    if ( loadSutraPopupLayoutMode() == SutraPopupLayoutMode::FitToResults ) {
      fitSutraPopupToResults( this, tabWidget );
    }
    positionSutraPopupCornerTools( this );

    showStatusBarMessage( checked ? tr( "Web tab shown" ) : tr( "Web tab hidden" ), 3500 );
  } );

  connect( sutraPopupRestoreDefaultsAction, &QAction::triggered, this, [ this ] {
    resetSutraPopupAppearanceDefaults();
    applySutraPopupOpacity( this );
    applySutraPopupTheme( this, tabWidget );
    applyZoomFactor();
    refreshSutraCustomTabs( tabWidget, pendingWord, translateBox->translateLine()->text() );
    applySutraPopupFontSizeToTabs( tabWidget );
    applySutraPopupTheme( this, tabWidget );
    updateSutraPopupZoomIndicator( this );
    applySutraPopupLayoutMode( this, tabWidget );

    QTimer::singleShot( 0, this, [ this ] {
      positionSutraPopupCornerTools( this );
    } );

    showStatusBarMessage(
      tr( "Popup defaults restored: optional tabs hidden, Light theme, Auto layout, 100% zoom, 100% opacity, Ctrl + Right Click, automatic capture" ),
      5000 );
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
  sutraPopupCornerTools->setAttribute( Qt::WA_StyledBackground, true );
  sutraPopupCornerTools->setAutoFillBackground( true );
  sutraPopupCornerTools->setToolTip( tr( "Popup quick settings" ) );
  sutraPopupCornerTools->setStyleSheet( sutraPopupCornerToolsThemeStyleSheet() );

  QHBoxLayout * sutraPopupCornerLayout = new QHBoxLayout( sutraPopupCornerTools );
  sutraPopupCornerLayout->setContentsMargins( 5, 3, 5, 3 );
  sutraPopupCornerLayout->setSpacing( 3 );

  const auto openSutraHistoryTerm = [ this ]( const QString & term, bool preserveHistoryPosition ) {
    const QString normalized = normalizeSmartLookupInput( term );
    if ( normalized.isEmpty() ) {
      return;
    }

    if ( preserveHistoryPosition ) {
      const QStringList history = loadSutraLookupHistory();
      const int index           = history.lastIndexOf( normalized );
      if ( index >= 0 ) {
        setProperty( "sutraLookupHistoryIndex", index );
        setProperty( "sutraLookupHistoryNavigating", true );
      }
    }
    translateWord( normalized );
  };

  const auto navigateSutraLookupHistory = [ this, openSutraHistoryTerm ]( int direction ) {
    const QStringList history = loadSutraLookupHistory();
    if ( history.isEmpty() ) {
      return;
    }

    int index = property( "sutraLookupHistoryIndex" ).toInt();
    if ( index < 0 || index >= history.size() ) {
      index = history.size() - 1;
    }

    const int targetIndex = qBound( 0, index + direction, history.size() - 1 );
    if ( targetIndex == index ) {
      updateSutraLookupFeatureControls( this );
      return;
    }

    setProperty( "sutraLookupHistoryIndex", targetIndex );
    setProperty( "sutraLookupHistoryNavigating", true );
    openSutraHistoryTerm( history.at( targetIndex ), false );
  };

  QMenu * sutraQuickCopyMenu = new QMenu( tr( "Quick copy" ), sutraPopupCornerTools );
  QAction * sutraCopyOriginalAction = sutraQuickCopyMenu->addAction( tr( "Copy original text" ) );
  QAction * sutraCopyHanVietAction  = sutraQuickCopyMenu->addAction( tr( "Copy Han-Viet" ) );
  QAction * sutraCopyMeaningAction  = sutraQuickCopyMenu->addAction( tr( "Copy meaning" ) );
  QAction * sutraCopyFullAction     = sutraQuickCopyMenu->addAction( tr( "Copy full result" ) );

  const auto copyCurrentSutraLookup = [ this ]( int copyMode ) {
    const SutraLookupDetails details = sutraLookupDetailsForTerm( sutraCurrentLookupTerm( this ) );
    QString text;
    switch ( copyMode ) {
      case 0:
        text = details.original;
        break;
      case 1:
        text = details.hanViet;
        break;
      case 2:
        text = details.meaning;
        break;
      default:
        text = sutraFullCopyText( details );
        break;
    }

    if ( text.isEmpty() ) {
      showStatusBarMessage( tr( "No matching text is available to copy" ), 3500 );
      return;
    }

    copySutraTextToClipboard( text );
    showStatusBarMessage( tr( "Copied to clipboard" ), 2500 );
  };

  connect( sutraCopyOriginalAction, &QAction::triggered, this, [ copyCurrentSutraLookup ] {
    copyCurrentSutraLookup( 0 );
  } );
  connect( sutraCopyHanVietAction, &QAction::triggered, this, [ copyCurrentSutraLookup ] {
    copyCurrentSutraLookup( 1 );
  } );
  connect( sutraCopyMeaningAction, &QAction::triggered, this, [ copyCurrentSutraLookup ] {
    copyCurrentSutraLookup( 2 );
  } );
  connect( sutraCopyFullAction, &QAction::triggered, this, [ copyCurrentSutraLookup ] {
    copyCurrentSutraLookup( 3 );
  } );

  connect( sutraQuickCopyMenu, &QMenu::aboutToShow, this, [ = ] {
    const SutraLookupDetails details = sutraLookupDetailsForTerm( sutraCurrentLookupTerm( this ) );
    sutraCopyOriginalAction->setEnabled( !details.original.isEmpty() );
    sutraCopyHanVietAction->setEnabled( !details.hanViet.isEmpty() );
    sutraCopyMeaningAction->setEnabled( !details.meaning.isEmpty() );
    sutraCopyFullAction->setEnabled( !sutraFullCopyText( details ).isEmpty() );
  } );

  const auto toggleCurrentSutraFavorite = [ this ] {
    const QString term = sutraCurrentLookupTerm( this );
    if ( term.isEmpty() ) {
      return;
    }

    QStringList favorites = loadSutraLookupFavorites();
    const bool removing   = favorites.contains( term );
    if ( removing ) {
      favorites.removeAll( term );
    }
    else {
      favorites << term;
      favorites.removeDuplicates();
    }
    saveSutraLookupFavorites( favorites );
    updateSutraLookupFeatureControls( this, term );
    showStatusBarMessage( removing ? tr( "Removed from popup favorites" )
                                      : tr( "Added to popup favorites" ),
                          3000 );
  };

  QMenu * sutraLookupLibraryMenu = new QMenu( tr( "History and favorites" ), sutraPopupCornerTools );
  connect( sutraLookupLibraryMenu, &QMenu::aboutToShow, this, [ = ] {
    sutraLookupLibraryMenu->clear();

    const QStringList history   = loadSutraLookupHistory();
    const QStringList favorites = loadSutraLookupFavorites();
    const QString currentTerm   = sutraCurrentLookupTerm( this );

    int historyIndex = property( "sutraLookupHistoryIndex" ).toInt();
    if ( history.isEmpty() ) {
      historyIndex = -1;
    }
    else if ( historyIndex < 0 || historyIndex >= history.size() ) {
      historyIndex = history.size() - 1;
    }

    QAction * previousLookupAction = sutraLookupLibraryMenu->addAction( tr( "Previous lookup" ) );
    QAction * nextLookupAction = sutraLookupLibraryMenu->addAction( tr( "Next lookup" ) );
    previousLookupAction->setShortcut( QKeySequence( Qt::ALT | Qt::Key_Left ) );
    nextLookupAction->setShortcut( QKeySequence( Qt::ALT | Qt::Key_Right ) );
    previousLookupAction->setEnabled( historyIndex > 0 );
    nextLookupAction->setEnabled( historyIndex >= 0 && historyIndex + 1 < history.size() );
    connect( previousLookupAction, &QAction::triggered, this, [ navigateSutraLookupHistory ] {
      navigateSutraLookupHistory( -1 );
    } );
    connect( nextLookupAction, &QAction::triggered, this, [ navigateSutraLookupHistory ] {
      navigateSutraLookupHistory( 1 );
    } );
    sutraLookupLibraryMenu->addSeparator();

    // Keep productivity features available without occupying permanent space
    // in the compact bottom toolbar.
    sutraLookupLibraryMenu->addMenu( sutraQuickCopyMenu );
    QAction * toggleFavoriteAction = sutraLookupLibraryMenu->addAction(
      favorites.contains( currentTerm ) ? tr( "Remove current lookup from favorites" )
                                        : tr( "Add current lookup to favorites" ) );
    toggleFavoriteAction->setEnabled( !currentTerm.isEmpty() );
    connect( toggleFavoriteAction, &QAction::triggered, this, toggleCurrentSutraFavorite );
    sutraLookupLibraryMenu->addSeparator();

    QAction * recentHeader = sutraLookupLibraryMenu->addAction( tr( "Recent lookups" ) );
    recentHeader->setEnabled( false );
    if ( history.isEmpty() ) {
      QAction * emptyHistory = sutraLookupLibraryMenu->addAction( tr( "No lookup history" ) );
      emptyHistory->setEnabled( false );
    }
    else {
      const int first = qMax( 0, history.size() - 20 );
      for ( int i = history.size() - 1; i >= first; --i ) {
        const QString term = history.at( i );
        QString label      = term;
        if ( label.size() > 64 ) {
          label = label.left( 61 ) + QStringLiteral( "..." );
        }
        QAction * action = sutraLookupLibraryMenu->addAction( label );
        action->setToolTip( term );
        connect( action, &QAction::triggered, this, [ openSutraHistoryTerm, term ] {
          openSutraHistoryTerm( term, true );
        } );
      }
    }

    sutraLookupLibraryMenu->addSeparator();
    QAction * favoritesHeader = sutraLookupLibraryMenu->addAction( tr( "Popup favorites" ) );
    favoritesHeader->setEnabled( false );
    if ( favorites.isEmpty() ) {
      QAction * emptyFavorites = sutraLookupLibraryMenu->addAction( tr( "No popup favorites" ) );
      emptyFavorites->setEnabled( false );
    }
    else {
      for ( const QString & term : favorites ) {
        QString label = term;
        if ( label.size() > 64 ) {
          label = label.left( 61 ) + QStringLiteral( "..." );
        }
        QAction * action = sutraLookupLibraryMenu->addAction( label );
        action->setToolTip( term );
        connect( action, &QAction::triggered, this, [ openSutraHistoryTerm, term ] {
          openSutraHistoryTerm( term, false );
        } );
      }
    }

    sutraLookupLibraryMenu->addSeparator();
    QAction * exportCsvAction = sutraLookupLibraryMenu->addAction( tr( "Export CSV..." ) );
    QAction * exportJsonAction = sutraLookupLibraryMenu->addAction( tr( "Export JSON..." ) );
    exportCsvAction->setEnabled( !history.isEmpty() || !favorites.isEmpty() );
    exportJsonAction->setEnabled( !history.isEmpty() || !favorites.isEmpty() );
    connect( exportCsvAction, &QAction::triggered, this, [ this ] {
      if ( exportSutraLookupDataCsv( this ) ) {
        showStatusBarMessage( tr( "Popup data exported as CSV" ), 3500 );
      }
    } );
    connect( exportJsonAction, &QAction::triggered, this, [ this ] {
      if ( exportSutraLookupDataJson( this ) ) {
        showStatusBarMessage( tr( "Popup data exported as JSON" ), 3500 );
      }
    } );

    sutraLookupLibraryMenu->addSeparator();
    QAction * clearHistoryAction = sutraLookupLibraryMenu->addAction( tr( "Clear lookup history" ) );
    QAction * clearFavoritesAction = sutraLookupLibraryMenu->addAction( tr( "Clear popup favorites" ) );
    clearHistoryAction->setEnabled( !history.isEmpty() );
    clearFavoritesAction->setEnabled( !favorites.isEmpty() );

    connect( clearHistoryAction, &QAction::triggered, this, [ this ] {
      if ( QMessageBox::question( this,
                                  tr( "Clear lookup history" ),
                                  tr( "Remove all saved popup lookup history?" ) )
           != QMessageBox::Yes ) {
        return;
      }
      saveSutraLookupHistory( {} );
      setProperty( "sutraLookupHistoryIndex", -1 );
      updateSutraLookupFeatureControls( this );
      showStatusBarMessage( tr( "Lookup history cleared" ), 3000 );
    } );

    connect( clearFavoritesAction, &QAction::triggered, this, [ this ] {
      if ( QMessageBox::question( this,
                                  tr( "Clear popup favorites" ),
                                  tr( "Remove all saved popup favorites?" ) )
           != QMessageBox::Yes ) {
        return;
      }
      saveSutraLookupFavorites( {} );
      updateSutraLookupFeatureControls( this );
      showStatusBarMessage( tr( "Popup favorites cleared" ), 3000 );
    } );
  } );

  QToolButton * sutraHistoryBackButton = new QToolButton( sutraPopupCornerTools );
  sutraHistoryBackButton->setObjectName( sutraHistoryBackButtonObjectName() );
  sutraHistoryBackButton->setIcon( sutraPopupNavigationIcon( false, sutraPopupActiveTextColor() ) );
  sutraHistoryBackButton->setIconSize( QSize( 18, 18 ) );
  sutraHistoryBackButton->setToolTip( tr( "Previous lookup (Alt+Left)" ) );
  sutraHistoryBackButton->setAccessibleName( tr( "Previous lookup" ) );
  sutraHistoryBackButton->setAutoRaise( true );

  QToolButton * sutraHistoryForwardButton = new QToolButton( sutraPopupCornerTools );
  sutraHistoryForwardButton->setObjectName( sutraHistoryForwardButtonObjectName() );
  sutraHistoryForwardButton->setIcon( sutraPopupNavigationIcon( true, sutraPopupActiveTextColor() ) );
  sutraHistoryForwardButton->setIconSize( QSize( 18, 18 ) );
  sutraHistoryForwardButton->setToolTip( tr( "Next lookup (Alt+Right)" ) );
  sutraHistoryForwardButton->setAccessibleName( tr( "Next lookup" ) );
  sutraHistoryForwardButton->setAutoRaise( true );

  QToolButton * sutraHistoryButton = new QToolButton( sutraPopupCornerTools );
  sutraHistoryButton->setObjectName( sutraHistoryButtonObjectName() );
  sutraHistoryButton->setIcon( sutraPopupHistoryIcon( sutraPopupActiveTextColor() ) );
  sutraHistoryButton->setIconSize( QSize( 18, 18 ) );
  sutraHistoryButton->setToolTip( tr( "Lookup history and favorites" ) );
  sutraHistoryButton->setAccessibleName( tr( "Lookup history and favorites" ) );
  sutraHistoryButton->setMenu( sutraLookupLibraryMenu );
  sutraHistoryButton->setPopupMode( QToolButton::InstantPopup );
  sutraHistoryButton->setAutoRaise( true );

  QToolButton * sutraNoteButton = new QToolButton( sutraPopupCornerTools );
  sutraNoteButton->setObjectName( sutraNoteButtonObjectName() );
  sutraNoteButton->setIcon( sutraPopupNoteIcon( sutraPopupActiveTextColor(), false ) );
  sutraNoteButton->setIconSize( QSize( 18, 18 ) );
  sutraNoteButton->setAccessibleName( tr( "Personal note" ) );
  sutraNoteButton->setAutoRaise( true );

  connect( sutraHistoryBackButton, &QToolButton::clicked, this, [ navigateSutraLookupHistory ] {
    navigateSutraLookupHistory( -1 );
  } );
  connect( sutraHistoryForwardButton, &QToolButton::clicked, this, [ navigateSutraLookupHistory ] {
    navigateSutraLookupHistory( 1 );
  } );

  connect( sutraNoteButton, &QToolButton::clicked, this, [ this ] {
    const QString term = sutraCurrentLookupTerm( this );
    if ( term.isEmpty() ) {
      return;
    }

    QVariantMap notes = loadSutraLookupNotes();
    bool accepted     = false;
    const QString note = QInputDialog::getMultiLineText( this,
                                                         tr( "Personal note" ),
                                                         tr( "Note for \"%1\":" ).arg( term ),
                                                         notes.value( term ).toString(),
                                                         &accepted );
    if ( !accepted ) {
      return;
    }

    if ( note.trimmed().isEmpty() ) {
      notes.remove( term );
      showStatusBarMessage( tr( "Personal note removed" ), 3000 );
    }
    else {
      notes.insert( term, note.left( 5000 ) );
      showStatusBarMessage( tr( "Personal note saved" ), 3000 );
    }
    saveSutraLookupNotes( notes );
    updateSutraLookupFeatureControls( this, term );
  } );

  QAction * sutraHistoryBackAction = new QAction( this );
  sutraHistoryBackAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  sutraHistoryBackAction->setShortcut( QKeySequence( Qt::ALT | Qt::Key_Left ) );
  addAction( sutraHistoryBackAction );
  connect( sutraHistoryBackAction, &QAction::triggered, this, [ navigateSutraLookupHistory ] {
    navigateSutraLookupHistory( -1 );
  } );

  QAction * sutraHistoryForwardAction = new QAction( this );
  sutraHistoryForwardAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  sutraHistoryForwardAction->setShortcut( QKeySequence( Qt::ALT | Qt::Key_Right ) );
  addAction( sutraHistoryForwardAction );
  connect( sutraHistoryForwardAction, &QAction::triggered, this, [ navigateSutraLookupHistory ] {
    navigateSutraLookupHistory( 1 );
  } );

  const auto createSutraTabShortcut = [ this ]( const QKeySequence & sequence,
                                                 const std::function< void() > & callback ) {
    QAction * action = new QAction( this );
    action->setShortcutContext( Qt::WidgetWithChildrenShortcut );
    action->setShortcut( sequence );
    addAction( action );
    connect( action, &QAction::triggered, this, [ callback ] {
      callback();
    } );
  };

  createSutraTabShortcut( QKeySequence( Qt::CTRL | Qt::Key_1 ), [ this ] {
    if ( tabWidget->count() > 0 ) {
      tabWidget->setCurrentIndex( 0 );
    }
  } );
  createSutraTabShortcut( QKeySequence( Qt::CTRL | Qt::Key_2 ), [ this ] {
    if ( QTextBrowser * browser = findBuddhistGlossaryBrowser( tabWidget ) ) {
      tabWidget->setCurrentWidget( browser );
    }
  } );
  createSutraTabShortcut( QKeySequence( Qt::CTRL | Qt::Key_3 ), [ this ] {
    if ( QTextBrowser * browser = findWebReferenceBrowser( tabWidget ) ) {
      tabWidget->setCurrentWidget( browser );
    }
  } );
  createSutraTabShortcut( QKeySequence( Qt::CTRL | Qt::Key_Tab ), [ this ] {
    const int count = tabWidget->count();
    if ( count > 1 ) {
      tabWidget->setCurrentIndex( ( tabWidget->currentIndex() + 1 ) % count );
    }
  } );
  createSutraTabShortcut( QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_Tab ), [ this ] {
    const int count = tabWidget->count();
    if ( count > 1 ) {
      tabWidget->setCurrentIndex( ( tabWidget->currentIndex() - 1 + count ) % count );
    }
  } );

  QToolButton * sutraPopupZoomOutButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupZoomOutButton->setObjectName( QStringLiteral( "sutraPopupZoomOutButton" ) );
  sutraPopupZoomOutButton->setText( QStringLiteral( "-" ) );
  sutraPopupZoomOutButton->setAccessibleName( tr( "Zoom out" ) );
  sutraPopupZoomOutButton->setToolTip( tr( "Zoom out (Ctrl+-)" ) );
  sutraPopupZoomOutButton->setAutoRaise( true );

  QToolButton * sutraPopupZoomIndicator = new QToolButton( sutraPopupCornerTools );
  sutraPopupZoomIndicator->setObjectName( sutraPopupZoomIndicatorObjectName() );
  sutraPopupZoomIndicator->setAccessibleName( tr( "Reset popup zoom" ) );
  sutraPopupZoomIndicator->setMinimumWidth( 46 );
  sutraPopupZoomIndicator->setToolTip( tr( "Reset zoom to 100% (Ctrl+0)" ) );
  sutraPopupZoomIndicator->setAutoRaise( true );

  QToolButton * sutraPopupZoomInButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupZoomInButton->setObjectName( QStringLiteral( "sutraPopupZoomInButton" ) );
  sutraPopupZoomInButton->setText( QStringLiteral( "+" ) );
  sutraPopupZoomInButton->setAccessibleName( tr( "Zoom in" ) );
  sutraPopupZoomInButton->setToolTip( tr( "Zoom in (Ctrl++)" ) );
  sutraPopupZoomInButton->setAutoRaise( true );

  QToolButton * sutraPopupThemeToggleButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupThemeToggleButton->setObjectName( sutraPopupThemeToggleButtonObjectName() );
  sutraPopupThemeToggleButton->setAccessibleName( tr( "Toggle popup theme" ) );
  sutraPopupThemeToggleButton->setMinimumWidth( 26 );
  sutraPopupThemeToggleButton->setIconSize( QSize( 20, 20 ) );
  sutraPopupThemeToggleButton->setToolButtonStyle( Qt::ToolButtonIconOnly );
  sutraPopupThemeToggleButton->setAutoRaise( true );

  QToolButton * sutraPopupColorsButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupColorsButton->setObjectName( sutraPopupColorsButtonObjectName() );
  sutraPopupColorsButton->setAccessibleName( tr( "Popup colors" ) );
  sutraPopupColorsButton->setMinimumWidth( 24 );
  sutraPopupColorsButton->setAutoRaise( true );
  sutraPopupColorsButton->setIcon( sutraPopupColorSwatchIcon( sutraPopupActiveBackgroundColor() ) );
  sutraPopupColorsButton->setToolTip( tr( "Popup text and background colors" ) );

  QToolButton * sutraPopupOptionsButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupOptionsButton->setObjectName( QStringLiteral( "sutraPopupOptionsButton" ) );
  sutraPopupOptionsButton->setText( QString() );
  sutraPopupOptionsButton->setIcon( sutraPopupSettingsIcon( sutraPopupActiveTextColor() ) );
  sutraPopupOptionsButton->setIconSize( QSize( 20, 20 ) );
  sutraPopupOptionsButton->setAccessibleName( tr( "Popup settings" ) );
  sutraPopupOptionsButton->setToolTip( tr( "Popup settings" ) );
  sutraPopupOptionsButton->setAutoRaise( true );

  QToolButton * sutraPopupBackToTopButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupBackToTopButton->setObjectName( QStringLiteral( "sutraPopupBackToTopButton" ) );
  sutraPopupBackToTopButton->setText( QString() );
  sutraPopupBackToTopButton->setIcon( sutraPopupBackToTopIcon( sutraPopupActiveTextColor() ) );
  sutraPopupBackToTopButton->setIconSize( QSize( 18, 18 ) );
  sutraPopupBackToTopButton->setAccessibleName( tr( "Back to top" ) );
  sutraPopupBackToTopButton->setToolTip( tr( "Back to top" ) );
  sutraPopupBackToTopButton->setAutoRaise( true );

  QToolButton * sutraPopupQuickFixButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupQuickFixButton->setObjectName( QStringLiteral( "sutraPopupQuickFixButton" ) );
  sutraPopupQuickFixButton->setText( QString() );
  sutraPopupQuickFixButton->setIcon( sutraPopupFixedLayoutIcon( sutraPopupActiveTextColor() ) );
  sutraPopupQuickFixButton->setIconSize( QSize( 18, 18 ) );
  sutraPopupQuickFixButton->setAccessibleName( tr( "Fix current size and position" ) );
  sutraPopupQuickFixButton->setToolTip( tr( "Fix current size and position" ) );
  sutraPopupQuickFixButton->setAutoRaise( true );

  QToolButton * sutraPopupQuickFitButton = new QToolButton( sutraPopupCornerTools );
  sutraPopupQuickFitButton->setObjectName( QStringLiteral( "sutraPopupQuickFitButton" ) );
  sutraPopupQuickFitButton->setText( QStringLiteral( "▣" ) );
  sutraPopupQuickFitButton->setAccessibleName( tr( "Fit window size to results" ) );
  sutraPopupQuickFitButton->setToolTip( tr( "Fit window size to results" ) );
  sutraPopupQuickFitButton->setAutoRaise( true );

  const auto createSutraToolbarSeparator = [ sutraPopupCornerTools ]( const QString & objectName ) {
    QFrame * separator = new QFrame( sutraPopupCornerTools );
    separator->setObjectName( objectName );
    separator->setProperty( "sutraToolbarSeparator", true );
    separator->setFrameShape( QFrame::VLine );
    separator->setFrameShadow( QFrame::Plain );
    separator->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Expanding );
    return separator;
  };

  QFrame * sutraSeparatorHistoryZoom =
    createSutraToolbarSeparator( QStringLiteral( "sutraSeparatorHistoryZoom" ) );
  QFrame * sutraSeparatorZoomAppearance =
    createSutraToolbarSeparator( QStringLiteral( "sutraSeparatorZoomAppearance" ) );
  QFrame * sutraSeparatorAppearanceLayout =
    createSutraToolbarSeparator( QStringLiteral( "sutraSeparatorAppearanceLayout" ) );

  sutraPopupCornerLayout->addWidget( sutraHistoryBackButton );
  sutraPopupCornerLayout->addWidget( sutraHistoryForwardButton );
  sutraPopupCornerLayout->addWidget( sutraHistoryButton );
  sutraPopupCornerLayout->addWidget( sutraNoteButton );
  sutraPopupCornerLayout->addWidget( sutraSeparatorHistoryZoom );
  sutraPopupCornerLayout->addWidget( sutraPopupZoomOutButton );
  sutraPopupCornerLayout->addWidget( sutraPopupZoomIndicator );
  sutraPopupCornerLayout->addWidget( sutraPopupZoomInButton );
  sutraPopupCornerLayout->addWidget( sutraSeparatorZoomAppearance );
  sutraPopupCornerLayout->addWidget( sutraPopupThemeToggleButton );
  sutraPopupCornerLayout->addWidget( sutraPopupColorsButton );
  sutraPopupCornerLayout->addWidget( sutraSeparatorAppearanceLayout );
  sutraPopupCornerLayout->addWidget( sutraPopupBackToTopButton );
  sutraPopupCornerLayout->addWidget( sutraPopupOptionsButton );
  sutraPopupCornerLayout->addWidget( sutraPopupQuickFixButton );
  sutraPopupCornerLayout->addWidget( sutraPopupQuickFitButton );

  connect( sutraPopupZoomOutButton, &QToolButton::clicked, sutraPopupZoomOutAction, &QAction::trigger );
  connect( sutraPopupZoomIndicator, &QToolButton::clicked, sutraPopupZoomResetAction, &QAction::trigger );
  connect( sutraPopupZoomInButton, &QToolButton::clicked, sutraPopupZoomInAction, &QAction::trigger );
  connect( sutraPopupThemeToggleButton, &QToolButton::clicked, sutraPopupThemeToggleAction, &QAction::trigger );
  connect( sutraPopupColorsButton, &QToolButton::clicked, this, [ = ] {
    positionSutraPopupCornerTools( this );
    sutraPopupColorsMenu->exec(
      sutraPopupColorsButton->mapToGlobal( QPoint( 0, sutraPopupColorsButton->height() ) ) );
  } );

  // Receive Ctrl + mouse-wheel events from every widget inside the popup,
  // including the article view, custom Definition/Web tabs and their scroll areas.
  // New descendants are covered by the ChildAdded branch in eventFilter().
  for ( QObject * popupObject : findChildren< QObject * >() ) {
    popupObject->installEventFilter( this );
  }

  connect( sutraPopupBackToTopButton, &QToolButton::clicked, this, [ this ] {
    QWidget * currentTab = tabWidget ? tabWidget->currentWidget() : nullptr;
    bool handled = false;

    if ( ArticleView * article = qobject_cast< ArticleView * >( currentTab ) ) {
      article->page()->runJavaScript( QStringLiteral(
        "window.scrollTo(0,0);"
        "document.documentElement.scrollTop=0;"
        "if(document.body){document.body.scrollTop=0;}" ) );
      handled = true;
    }

    if ( QTextBrowser * browser = qobject_cast< QTextBrowser * >( currentTab ) ) {
      browser->verticalScrollBar()->setValue( browser->verticalScrollBar()->minimum() );
      handled = true;
    }
    else if ( currentTab ) {
      const QList< QTextBrowser * > browsers = currentTab->findChildren< QTextBrowser * >();
      for ( QTextBrowser * browser : browsers ) {
        browser->verticalScrollBar()->setValue( browser->verticalScrollBar()->minimum() );
        handled = true;
      }
    }

    if ( handled ) {
      showStatusBarMessage( tr( "Back to top" ), 1800 );
    }
  } );

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

  updateSutraPopupZoomIndicator( this );
  updateSutraLookupFeatureControls( this );
  applySutraPopupPointingCursors( this );
  sutraPopupCornerTools->show();
  positionSutraPopupCornerTools( this );

  applySutraPopupTheme( this, tabWidget );
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
  applySutraPopupFontSizeToTabs( tabWidget );
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
  applySutraPopupThemeToArticleView( definition );

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
  const SutraMouseLookupCaptureMode captureMode = loadSutraMouseLookupCaptureMode();
  const bool manualCapture = captureMode == SutraMouseLookupCaptureMode::SelectedText;
  const bool entirePhraseCapture = captureMode == SutraMouseLookupCaptureMode::EntirePhrase;

  // Manual capture and Entire Phrase capture both preserve the exact incoming
  // text. Precise automatic capture keeps the existing smart-term reduction.
  pendingWord = ( manualCapture || entirePhraseCapture )
                  ? normalizedWord
                  : chooseSmartLookupQuery( normalizedWord, smartTerms );

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

  if ( manualCapture ) {
    showStatusBarMessage( tr( "Manual selection lookup" ), 3000 );
  }
  else if ( entirePhraseCapture ) {
    showStatusBarMessage( tr( "Entire phrase lookup" ), 3000 );
  }
  else if ( !smartTerms.isEmpty() && pendingWord != normalizedWord ) {
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
  sutraRestorePopupFromMinimized( this );
  QTimer::singleShot( 0, this, [ this ] {
    sutraApplyFitToResultsPopup( this, tabWidget );
  } );

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
  recordSutraLookupHistory( const_cast< ScanPopup * >( this ), word );
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
  const auto belongsToThisPopup = [ this ]( QObject * object ) {
    for ( ; object; object = object->parent() ) {
      if ( object == this ) {
        return true;
      }
    }
    return false;
  };

  // Keep the popup-scoped event filter attached to widgets created later
  // (for example internal web/article view children). Guarding by ancestry
  // is important because this filter is briefly installed on qApp by the
  // existing mouse-interception flow.
  if ( event->type() == QEvent::ChildAdded && belongsToThisPopup( watched ) ) {
    const QChildEvent * childEvent = static_cast< QChildEvent * >( event );
    QObject * child               = childEvent->child();

    if ( child ) {
      child->installEventFilter( this );
      applySutraPointingCursorToObject( child );
      for ( QObject * descendant : child->findChildren< QObject * >() ) {
        descendant->installEventFilter( this );
        applySutraPointingCursorToObject( descendant );
      }
    }
  }

  if ( event->type() == QEvent::Wheel && belongsToThisPopup( watched ) ) {
    QWheelEvent * wheelEvent = static_cast< QWheelEvent * >( event );

    if ( wheelEvent->modifiers().testFlag( Qt::ControlModifier ) ) {
      int delta     = wheelEvent->angleDelta().y();
      int threshold = 120;

      // Precision touchpads can report pixel deltas instead of classic
      // 120-unit wheel steps. Accumulate them to avoid over-sensitive zoom.
      if ( delta == 0 ) {
        delta     = wheelEvent->pixelDelta().y();
        threshold = 40;
      }

      int accumulatedDelta = property( "sutraPopupWheelZoomDelta" ).toInt() + delta;
      QAction * zoomAction = nullptr;

      if ( accumulatedDelta >= threshold ) {
        zoomAction = findChild< QAction * >( QStringLiteral( "sutraPopupZoomInAction" ) );
        accumulatedDelta %= threshold;
      }
      else if ( accumulatedDelta <= -threshold ) {
        zoomAction = findChild< QAction * >( QStringLiteral( "sutraPopupZoomOutAction" ) );
        accumulatedDelta = -( ( -accumulatedDelta ) % threshold );
      }

      setProperty( "sutraPopupWheelZoomDelta", accumulatedDelta );

      if ( zoomAction ) {
        zoomAction->trigger();
      }

      wheelEvent->accept();
      return true;
    }

    // Do not carry a partial touchpad delta into a later Ctrl+wheel gesture.
    setProperty( "sutraPopupWheelZoomDelta", 0 );
  }

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
    sutraApplyNativeTopmost( this, ui.onTopButton->isChecked() );
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

void ScanPopup::pageLoaded( ArticleView * view ) const
{
  // Theme the page even if it finishes loading while the popup is hidden.
  // Otherwise the next popup open can briefly retain a light document canvas.
  applySutraPopupThemeToArticleView( view );

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
  cfg.popupWindowAlwaysOnTop = checked;

  if ( !ui.pinButton->isChecked() ) {
    return;
  }

  const bool wasVisible = isVisible();
  const QByteArray geometryBeforeFlags = saveGeometry();

  Qt::WindowFlags flags = pinnedWindowFlags;
  if ( checked ) {
    flags |= Qt::WindowStaysOnTopHint;
  }

  setWindowFlags( flags );

  if ( wasVisible ) {
    show();
    if ( !geometryBeforeFlags.isEmpty() ) {
      restoreGeometry( geometryBeforeFlags );
    }
  }

  sutraApplyNativeTopmost( this, checked );
  if ( checked ) {
    raise();
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
        applySutraPopupThemeToArticleView( view );
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

  applySutraPopupThemeToArticleView( view );
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
