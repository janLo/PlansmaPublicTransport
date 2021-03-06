/*
*   Copyright 2012 Friedrich Pülz <fpuelz@gmx.de>
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU Library General Public License as
*   published by the Free Software Foundation; either version 2 or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details
*
*   You should have received a copy of the GNU Library General Public
*   License along with this program; if not, write to the
*   Free Software Foundation, Inc.,
*   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "StopSuggestionsTest.h"

#include <Plasma/DataEngineManager>

#include <QtTest/QTest>
#include <QTimer>

void StopSuggestionsTest::initTestCase()
{
    Plasma::DataEngineManager *manager = Plasma::DataEngineManager::self();
    m_publicTransportEngine = manager->loadEngine( "publictransport" );
}

void StopSuggestionsTest::init()
{}

void StopSuggestionsTest::cleanup()
{}

void StopSuggestionsTest::cleanupTestCase()
{
    Plasma::DataEngineManager *manager = Plasma::DataEngineManager::self();
    manager->unloadEngine( "publictransport" );
}

void StopSuggestionsTest::stopSuggestionTest_data()
{
    QTest::addColumn<QString>("serviceProvider");
    QTest::addColumn<QString>("city");
    QTest::addColumn<QString>("stopName");
    QTest::addColumn<bool>("containsIDs");
    QTest::addColumn<bool>("containsWeights");

    // 21 service provider plugins
    QTest::newRow("at_oebb") << "at_oebb" << QString() << "Wien" << true << true;
    QTest::newRow("be_brail") << "be_brail" << QString() << "Brüssel" << true << false;
    QTest::newRow("ch_sbb") << "ch_sbb" << QString() << "Bern" << true << false;
    QTest::newRow("cz_idnes") << "cz_idnes" << "Brno" << "Technolog" << false << false;
    QTest::newRow("de_bvg") << "de_bvg" << QString() << "Alexander" << false << false;
    QTest::newRow("de_db") << "de_db" << QString() << "Bremen Hbf" << true << true;
    QTest::newRow("de_dvb") << "de_dvb" << QString() << "Hauptbahnhof" << false << false;
    QTest::newRow("de_fahrplaner") << "de_fahrplaner" << QString() << "Bremen Hbf" << true << false;
    QTest::newRow("de_nasa") << "de_nasa" << QString() << "Kirkel" << false << false;
    QTest::newRow("de_rmv") << "de_rmv" << QString() << "Frankfurt" << false << false;
//     QTest::newRow("de_vvs") << "de_vvs" << "Stuttgart" << "Herren" << false << false; // Doesn't support stop suggestions
    QTest::newRow("dk_rejseplanen") << "dk_rejseplanen" << QString() << "Oslovej" << false << false;
//     QTest::newRow("fr_gares") << "fr_gares" << QString() << "Lyon" << true << false;
    QTest::newRow("it_cup2000") << "it_cup2000" << QString() << "Roma" << false << false;
    QTest::newRow("it_orario") << "it_orario" << QString() << "Genova" << false << false;
    QTest::newRow("pl_pkp") << "pl_pkp" << "Brno" << "Warszawa" << false << false;
//     QTest::newRow("sk_atlas") << "sk_atlas" << "bratislava" << "br" << true << false;
//     QTest::newRow("sk_imhd") << "sk_imhd" << "bratislava" << "br" << false << false;
    QTest::newRow("us_septa") << "us_septa" << QString() << "Pennsyl" << true << false;
    QTest::newRow("international_flightstats") << "international_flightstats" << QString() << "Bremen" << true << false;
}

void StopSuggestionsTest::stopSuggestionTest()
{
    QFETCH(QString, serviceProvider);
    QFETCH(QString, city);
    QFETCH(QString, stopName);
    QFETCH(bool, containsIDs);
    QFETCH(bool, containsWeights);

    // Connect source and wait until the dataUpdated slot gets called in testVisualization
    QString sourceName = QString("Stops %1|stop=%2").arg(serviceProvider).arg(stopName);
    if ( !city.isNull() ) {
        sourceName.append( QString("|city=%1").arg(city) );
    }
    QEventLoop loop;
    TestVisualization testVisualization;
    loop.connect( &testVisualization, SIGNAL(completed()), SLOT(quit()) );
    QTime timer;
    timer.start();
    m_publicTransportEngine->connectSource( sourceName, &testVisualization );
    QTimer::singleShot( 5000, &loop, SLOT(quit()) ); // timeout after 5 seconds
    loop.exec();

    m_publicTransportEngine->disconnectSource( sourceName, &testVisualization );
    qDebug() << QString("Got data from %1 and parsed it in %2 seconds")
            .arg( serviceProvider ).arg( timer.elapsed() / 1000.0 );

    // Test main keys
    QVERIFY( !testVisualization.data["error"].toBool() );
    QVERIFY( testVisualization.data.contains("stops") );
    QVERIFY( testVisualization.data["updated"].canConvert(QVariant::DateTime) );
//     QVERIFY( !testVisualization.data["serviceProvider"].toString().isEmpty() ); TODO
//     QVERIFY( !testVisualization.data["requestUrl"].toString().isEmpty() ); TODO
//     QVERIFY( QUrl(testVisualization.data["requestUrl"].toString()).isValid() ); TODO

    QVariantList stops = testVisualization.data["stops"].toList();
    QVERIFY( stops.count() > 0 );

    foreach ( const QVariant &stop, stops ) {
        QVariantHash stopData = stop.toHash();

        // Each stop object should contain some elements
        QVERIFY( !stopData.isEmpty() );

        // Ensure that these keys are in the hash and test it's data types
        QVERIFY( stopData.contains("StopName") );
        QVERIFY( stopData["StopName"].canConvert(QVariant::String) );

        if ( containsIDs ) {
            QVERIFY( stopData.contains("StopID") );
            QVERIFY( stopData["StopID"].canConvert(QVariant::String) );
        }

        if ( containsWeights ) {
            QVERIFY( stopData.contains("StopWeight") );
            QVERIFY( stopData["StopWeight"].canConvert(QVariant::Int) );
        }
    }

    QVERIFY2( !testVisualization.data.isEmpty(),
              QString("No data for source name '%1' in 5 seconds").arg(sourceName).toLatin1().data() );
}

QTEST_MAIN(StopSuggestionsTest)
#include "StopSuggestionsTest.moc"
