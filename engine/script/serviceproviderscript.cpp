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

// Header
#include "serviceproviderscript.h"

// Own includes
#include "scripting.h"
#include "script_thread.h"
#include "serviceproviderglobal.h"
#include "serviceproviderdata.h"
#include "serviceprovidertestdata.h"
#include "departureinfo.h"
#include "request.h"

// KDE includes
#include <KLocalizedString>
#include <KConfig>
#include <KConfigGroup>
#include <KStandardDirs>
#include <KDebug>
#include <ThreadWeaver/Weaver>
#include <ThreadWeaver/Job>

// Qt includes
#include <QTextCodec>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QMutex>
#include <QTimer>
#include <QEventLoop>
#include <QScriptProgram>
#include <QScriptEngine>
#include <QScriptContextInfo>

const char *ServiceProviderScript::SCRIPT_FUNCTION_FEATURES = "features";
const char *ServiceProviderScript::SCRIPT_FUNCTION_GETTIMETABLE = "getTimetable";
const char *ServiceProviderScript::SCRIPT_FUNCTION_GETJOURNEYS = "getJourneys";
const char *ServiceProviderScript::SCRIPT_FUNCTION_GETSTOPSUGGESTIONS = "getStopSuggestions";
const char *ServiceProviderScript::SCRIPT_FUNCTION_GETADDITIONALDATA = "getAdditionalData";

ServiceProviderScript::ServiceProviderScript( const ServiceProviderData *data, QObject *parent,
                                              const QSharedPointer<KConfig> &cache )
        : ServiceProvider(data, parent), m_thread(0), m_mutex(new QMutex)
{
    m_scriptState = WaitingForScriptUsage;
    m_scriptFeatures = readScriptFeatures( cache.isNull() ? ServiceProviderGlobal::cache() : cache );

    qRegisterMetaType< QList<ChangelogEntry> >( "QList<ChangelogEntry>" );
    qRegisterMetaType< TimetableData >( "TimetableData" );
    qRegisterMetaType< QList<TimetableData> >( "QList<TimetableData>" );
    qRegisterMetaType< GlobalTimetableInfo >( "GlobalTimetableInfo" );
    qRegisterMetaType< ParseDocumentMode >( "ParseDocumentMode" );
    qRegisterMetaType< ResultObject::Features >( "ResultObject::Features" );
    qRegisterMetaType< ResultObject::Hints >( "ResultObject::Hints" );

    // FIXME: Unfortunately this crashes with multiple QScriptEngine's importing the same extension
    // in different threads, ie. at the same time...
    // Maybe it helps to protect the importExtension() function
//     ThreadWeaver::Weaver::instance()->setMaximumNumberOfThreads( 1 );
}

ServiceProviderScript::~ServiceProviderScript()
{
    // Wait for running jobs to finish for proper cleanup
//     ThreadWeaver::Weaver::instance()->requestAbort();
//     ThreadWeaver::Weaver::instance()->finish(); // This prevents crashes on exit of the engine
    delete m_mutex;
}

QStringList ServiceProviderScript::allowedExtensions()
{
    return QStringList() << "kross" << "qt" << "qt.core" << "qt.xml";
}

bool ServiceProviderScript::lazyLoadScript()
{
    // Read script
    QFile scriptFile( m_data->scriptFileName() );
    if ( !scriptFile.open(QIODevice::ReadOnly) ) {
        kDebug() << "Script could not be opened for reading"
                 << m_data->scriptFileName() << scriptFile.errorString();
        return false;
    }
    QTextStream stream( &scriptFile );
    QString scriptContents = stream.readAll();
    scriptFile.close();

    // Initialize the script
    m_scriptData = ScriptData( m_data, QScriptProgram(scriptContents, m_data->scriptFileName()) );

    return true;
}

bool ServiceProviderScript::runTests( QString *errorMessage ) const
{
    if ( !QFile::exists(m_data->scriptFileName()) ) {
        if ( errorMessage ) {
            *errorMessage = i18nc("@info/plain", "Script file not found: <filename>%1</filename>",
                                  m_data->scriptFileName());
        }
        return false;
    }

    QFile scriptFile( m_data->scriptFileName() );
    if ( !scriptFile.open(QIODevice::ReadOnly) ) {
        if ( errorMessage ) {
            *errorMessage = i18nc("@info/plain", "Could not open script file: <filename>%1</filename>",
                                  m_data->scriptFileName());
        }
        return false;
    }

    QTextStream stream( &scriptFile );
    const QString program = stream.readAll();
    scriptFile.close();

    if ( program.isEmpty() ) {
        if ( errorMessage ) {
            *errorMessage = i18nc("@info/plain", "Script file is empty: %1", m_data->scriptFileName());
        }
        return false;
    }

    const QScriptSyntaxCheckResult syntax = QScriptEngine::checkSyntax( program );
    if ( syntax.state() != QScriptSyntaxCheckResult::Valid ) {
        if ( errorMessage ) {
            *errorMessage = i18nc("@info/plain",
                                  "Syntax error in script file, line %1: <message>%2</message>",
                                  syntax.errorLineNumber(), syntax.errorMessage().isEmpty()
                                  ? i18nc("@info/plain", "Syntax error") : syntax.errorMessage());
        }
        return false;
    }

    // No errors found
    return true;
}

bool ServiceProviderScript::isTestResultUnchanged( const QString &providerId,
                                                   const QSharedPointer< KConfig > &cache )
{
    const KConfigGroup providerGroup = cache->group( providerId );
    if ( !providerGroup.hasGroup("script") ) {
        // Not a scripted provider or modified time not stored yet
        return true;
    }

    // Check if included files have been marked as modified since the cache was last updated
    const KConfigGroup providerScriptGroup = providerGroup.group( "script" );
    const bool includesUpToDate = providerScriptGroup.readEntry( "includesUpToDate", false );
    if ( !includesUpToDate ) {
        // An included file was modified
        return false;
    }

    // Check if the script file was modified since the cache was last updated
    const QDateTime scriptModifiedTime = providerScriptGroup.readEntry("modifiedTime", QDateTime());
    const QString scriptFilePath = providerScriptGroup.readEntry( "scriptFileName", QString() );
    const QFileInfo scriptFileInfo( scriptFilePath );
    if ( scriptFileInfo.lastModified() != scriptModifiedTime ) {
        kDebug() << "Script was modified:" << scriptFileInfo.fileName();
        return false;
    }

    // Check all included files and update "includesUpToDate" fields in using providers
    if ( checkIncludedFiles(cache, providerId) ) {
        // An included file of the provider was modified
        return false;
    }

    return true;
}

bool ServiceProviderScript::isTestResultUnchanged( const QSharedPointer<KConfig> &cache ) const
{
    return isTestResultUnchanged( id(), cache );
}

QList<Enums::ProviderFeature> ServiceProviderScript::readScriptFeatures(
        const QSharedPointer<KConfig> &cache )
{
    // Check if the script file was modified since the cache was last updated
    KConfigGroup providerGroup = cache->group( m_data->id() );
    if ( providerGroup.hasGroup("script") && isTestResultUnchanged(cache) &&
         providerGroup.hasKey("features") )
    {
        // Return feature list stored in the cache
        bool ok;
        QStringList featureStrings = providerGroup.readEntry("features", QStringList());
        featureStrings.removeOne("(none)");
        QList<Enums::ProviderFeature> features =
                ServiceProviderGlobal::featuresFromFeatureStrings( featureStrings, &ok );
        if ( ok ) {
            // The stored feature list only contains valid strings
            return features;
        } else {
            kWarning() << "Invalid feature string stored for provider" << m_data->id();
        }
    }

    // No actual cached information about the service provider
    kDebug() << "No up-to-date cache information for service provider" << m_data->id();
    QList<Enums::ProviderFeature> features;
    QStringList includedFiles;
    bool ok = lazyLoadScript();
    QString errorMessage;
    if ( !ok ) {
        errorMessage = i18nc("@info/plain", "Cannot open script file <filename>%1</filename>",
                             m_data->scriptFileName());
    } else {
        // Create script engine
        QScriptEngine engine;
        foreach ( const QString &import, m_data->scriptExtensions() ) {
            if ( !importExtension(&engine, import) ) {
                ok = false;
                errorMessage = i18nc("@info/plain", "Cannot import script extension %1", import);
                break;
            }
        }
        if ( ok ) {
            ScriptObjects objects;
            objects.createObjects( m_scriptData );
            objects.attachToEngine( &engine, m_scriptData );

            engine.evaluate( m_scriptData.program );
            QVariantList result;
            if ( !engine.hasUncaughtException() ) {
                result = engine.globalObject().property(
                        SCRIPT_FUNCTION_FEATURES ).call().toVariant().toList();
            }
            if ( engine.hasUncaughtException() ) {
                kDebug() << "Error in the script" << engine.uncaughtExceptionLineNumber()
                         << engine.uncaughtException().toString();
                kDebug() << "Backtrace:" << engine.uncaughtExceptionBacktrace().join("\n");
                ok = false;
                errorMessage = i18nc("@info/plain", "Uncaught exception in script "
                                     "<filename>%1</filename>, line %2: <message>%3</message>",
                                     QFileInfo(QScriptContextInfo(engine.currentContext()).fileName()).fileName(),
                                     engine.uncaughtExceptionLineNumber(),
                                     engine.uncaughtException().toString());
            } else {
                includedFiles = engine.globalObject().property( "includedFiles" )
                                                     .toVariant().toStringList();

                // Test if specific functions exist in the script
                if ( engine.globalObject().property(SCRIPT_FUNCTION_GETSTOPSUGGESTIONS).isValid() ) {
                    features << Enums::ProvidesStopSuggestions;
                }
                if ( engine.globalObject().property(SCRIPT_FUNCTION_GETJOURNEYS).isValid() ) {
                    features << Enums::ProvidesJourneys;
                }

                // Test if features() script function is available
                if ( !engine.globalObject().property(SCRIPT_FUNCTION_FEATURES).isValid() ) {
                    kDebug() << "The script has no" << SCRIPT_FUNCTION_FEATURES << "function";
                } else {
                    // Use values returned by features() script functions
                    // to get additional features of the service provider
                    foreach ( const QVariant &value, result ) {
                        features << static_cast<Enums::ProviderFeature>( value.toInt() );
                    }
                }
            }
        }
    }

    // Update script modified time in cache
    KConfigGroup scriptGroup = providerGroup.group( "script" );
    scriptGroup.writeEntry( "scriptFileName", m_data->scriptFileName() );
    scriptGroup.writeEntry( "modifiedTime", QFileInfo(m_data->scriptFileName()).lastModified() );

    // Remove provider from cached data for include file(s) no longer used by the provider
    KConfigGroup globalScriptGroup = cache->group( "script" );
    const QStringList globalScriptGroupNames = globalScriptGroup.groupList();
    foreach ( const QString &globalScriptGroupName, globalScriptGroupNames ) {
        if ( !globalScriptGroupName.startsWith(QLatin1String("include_")) ) {
            continue;
        }

        QString includedFile = globalScriptGroupName;
        includedFile.remove( 0, 8 ); // Remove "include_" from beginning
        const QFileInfo fileInfo( includedFile );

        KConfigGroup includeFileGroup = globalScriptGroup.group( "include_" + fileInfo.filePath() );
        QStringList usingProviders = includeFileGroup.readEntry( "usingProviders", QStringList() );
        if ( usingProviders.contains(m_data->id()) && !includedFiles.contains(includedFile) ) {
            // This provider is marked as using the include file, but it no longer uses that file
            usingProviders.removeOne( m_data->id() );
            includeFileGroup.writeEntry( "usingProviders", usingProviders );
        }
    }

    // Check if included file(s) were modified
    foreach ( const QString &includedFile, includedFiles ) {
        // Add this provider to the list of providers using the current include file
        const QFileInfo fileInfo( includedFile );
        KConfigGroup includeFileGroup = globalScriptGroup.group( "include_" + fileInfo.filePath() );
        QStringList usingProviders = includeFileGroup.readEntry( "usingProviders", QStringList() );
        if ( !usingProviders.contains(m_data->id()) ) {
            usingProviders << m_data->id();
            includeFileGroup.writeEntry( "usingProviders", usingProviders );
        }

        // Check if the include file was modified
        checkIncludedFile( cache, fileInfo );
    }

    // Update modified times of included files
    scriptGroup.writeEntry( "includesUpToDate", true ); // Was just updated

    // Set error in default cache group
    if ( !ok ) {
        ServiceProviderTestData newTestData = ServiceProviderTestData::read( id(), cache );
        newTestData.setSubTypeTestStatus( ServiceProviderTestData::Failed, errorMessage );
        newTestData.write( id(), cache );
    }

    return features;
}

bool ServiceProviderScript::checkIncludedFile( const QSharedPointer<KConfig> &cache,
                                               const QFileInfo &fileInfo, const QString &providerId )
{
    // Use a config group in the global script group for each included file.
    // It stores the last modified time and a list of IDs of providers using the include file
    KConfigGroup globalScriptGroup = cache->group( "script" );
    KConfigGroup includeFileGroup = globalScriptGroup.group( "include_" + fileInfo.filePath() );
    const QDateTime lastModified = includeFileGroup.readEntry( "modifiedTime", QDateTime() );

    // Update "includesUpToDate" field of using providers, if the include file was modified
    if ( lastModified != fileInfo.lastModified() ) {
        // The include file was modified, update all using providers (later).
        // isTestResultUnchanged() returns false if "includesUpToDate" is false
        const QStringList usingProviders = includeFileGroup.readEntry( "usingProviders", QStringList() );
        foreach ( const QString &usingProvider, usingProviders ) {
            if ( cache->hasGroup(usingProvider) ) {
                KConfigGroup usingProviderScriptGroup =
                        cache->group( usingProvider ).group( "script" );
                usingProviderScriptGroup.writeEntry( "includesUpToDate", false );
            }
        }

        includeFileGroup.writeEntry( "modifiedTime", fileInfo.lastModified() );
        return providerId.isEmpty() || usingProviders.contains(providerId);
    } else {
        return false;
    }
}

bool ServiceProviderScript::checkIncludedFiles( const QSharedPointer<KConfig> &cache,
                                                const QString &providerId )
{
    bool modified = false;
    KConfigGroup globalScriptGroup = cache->group( "script" );
    const QStringList globalScriptGroups = globalScriptGroup.groupList();
    foreach ( const QString &globalScriptGroup, globalScriptGroups ) {
        if ( !globalScriptGroup.startsWith(QLatin1String("include_")) ) {
            continue;
        }

        QString includedFile = globalScriptGroup;
        includedFile.remove( 0, 8 ); // Remove "include_" from beginning

        const QFileInfo fileInfo( includedFile );
        if ( checkIncludedFile(cache, fileInfo, providerId) ) {
            // The include file was modified and is used by this provider
            modified = true;
        }
    }

    return modified;
}

QList<Enums::ProviderFeature> ServiceProviderScript::features() const
{
    return m_scriptFeatures;
}

void ServiceProviderScript::departuresReady( const QList<TimetableData> &data,
        ResultObject::Features features, ResultObject::Hints hints, const QString &url,
        const GlobalTimetableInfo &globalInfo, const DepartureRequest &request,
        bool couldNeedForcedUpdate )
{
//     TODO use hints
    if ( data.isEmpty() ) {
        kDebug() << "The script didn't find any departures" << request.sourceName;
        emit errorParsing( this, ErrorParsingFailed,
                           i18n("Error while parsing the departure document."), url, &request );
    } else {
        // Create PublicTransportInfo objects for new data and combine with already published data
        PublicTransportInfoList newResults;
        ResultObject::dataList( data, &newResults, request.parseMode,
                                m_data->defaultVehicleType(), &globalInfo, features, hints );
        PublicTransportInfoList results = (m_publishedData[request.sourceName] << newResults);
        DepartureInfoList departures;
        foreach( const PublicTransportInfoPtr &info, results ) {
            departures << info.dynamicCast<DepartureInfo>();
        }

        emit departureListReceived( this, url, departures, globalInfo, request );
        if ( couldNeedForcedUpdate ) {
            emit forceUpdate();
        }
    }
}

void ServiceProviderScript::arrivalsReady( const QList< TimetableData > &data,
        ResultObject::Features features, ResultObject::Hints hints, const QString &url,
        const GlobalTimetableInfo &globalInfo, const ArrivalRequest &request,
        bool couldNeedForcedUpdate )
{
//     TODO use hints
    if ( data.isEmpty() ) {
        kDebug() << "The script didn't find any arrivals" << request.sourceName;
        emit errorParsing( this, ErrorParsingFailed,
                           i18n("Error while parsing the arrival document."), url, &request );
    } else {
        // Create PublicTransportInfo objects for new data and combine with already published data
        PublicTransportInfoList newResults;
        ResultObject::dataList( data, &newResults, request.parseMode,
                                m_data->defaultVehicleType(), &globalInfo, features, hints );
        PublicTransportInfoList results = (m_publishedData[request.sourceName] << newResults);
        ArrivalInfoList arrivals;
        foreach( const PublicTransportInfoPtr &info, results ) {
            arrivals << info.dynamicCast<ArrivalInfo>();
        }

        emit arrivalListReceived( this, url, arrivals, globalInfo, request );
        if ( couldNeedForcedUpdate ) {
            emit forceUpdate();
        }
    }
}

void ServiceProviderScript::journeysReady( const QList<TimetableData> &data,
        ResultObject::Features features, ResultObject::Hints hints, const QString &url,
        const GlobalTimetableInfo &globalInfo, const JourneyRequest &request,
        bool couldNeedForcedUpdate )
{
    Q_UNUSED( couldNeedForcedUpdate );
//     TODO use hints
    if ( data.isEmpty() ) {
        kDebug() << "The script didn't find any journeys" << request.sourceName;
        emit errorParsing( this, ErrorParsingFailed,
                           i18n("Error while parsing the journey document."), url, &request );
    } else {
        // Create PublicTransportInfo objects for new data and combine with already published data
        PublicTransportInfoList newResults;
        ResultObject::dataList( data, &newResults, request.parseMode,
                                m_data->defaultVehicleType(), &globalInfo, features, hints );
        PublicTransportInfoList results =
                (m_publishedData[request.sourceName] << newResults);
//         Q_ASSERT( request.parseMode == ParseForJourneys );
        JourneyInfoList journeys;
        foreach( const PublicTransportInfoPtr &info, results ) {
            journeys << info.dynamicCast<JourneyInfo>();
        }

        emit journeyListReceived( this, url, journeys, globalInfo, request );
    }
}

void ServiceProviderScript::stopSuggestionsReady( const QList<TimetableData> &data,
        ResultObject::Features features, ResultObject::Hints hints, const QString &url,
        const GlobalTimetableInfo &globalInfo, const StopSuggestionRequest &request,
        bool couldNeedForcedUpdate )
{
    Q_UNUSED( couldNeedForcedUpdate );
//     TODO use hints
    kDebug() << "***** Received" << data.count() << "items";

    // Create PublicTransportInfo objects for new data and combine with already published data
    PublicTransportInfoList newResults;
    ResultObject::dataList( data, &newResults, request.parseMode,
                            m_data->defaultVehicleType(), &globalInfo, features, hints );
    PublicTransportInfoList results( m_publishedData[request.sourceName] << newResults );
    kDebug() << "RESULTS:" << results;

    StopInfoList stops;
    foreach( const PublicTransportInfoPtr &info, results ) {
        stops << info.dynamicCast<StopInfo>();
    }

    emit stopListReceived( this, url, stops, request );
}

void ServiceProviderScript::additionDataReady( const TimetableData &data,
        ResultObject::Features features, ResultObject::Hints hints, const QString &url,
        const GlobalTimetableInfo &globalInfo, const AdditionalDataRequest &request,
        bool couldNeedForcedUpdate )
{
    Q_UNUSED( couldNeedForcedUpdate );
    if ( data.isEmpty() ) {
        kDebug() << "The script didn't find any new data" << request.sourceName;
        emit errorParsing( this, ErrorParsingFailed,
                           i18nc("@info/plain", "No additional data found."),
                           url, &request );
    } else {
        // Create PublicTransportInfo objects for new data and combine with already published data
//         PublicTransportInfoList newResults;
//         ResultObject::dataList( data, &newResults, request.parseMode,
//                                 m_data->defaultVehicleType(), &globalInfo, features, hints );
//         PublicTransportInfoList results =
//                 (m_publishedData[request.sourceName] << newResults);

        kDebug() << "Additional data is ready:" << data;
        emit additionalDataReceived( this, url, data, request );
    }
}

void ServiceProviderScript::jobStarted( ThreadWeaver::Job* job )
{
    ScriptJob *scriptJob = qobject_cast< ScriptJob* >( job );
    Q_ASSERT( scriptJob );

    const QString sourceName = scriptJob->request()->sourceName;
    Q_ASSERT ( !m_publishedData.contains(sourceName) ); // TODO
//     {
//         qDebug() << "------------------------------------------------------------------------";
//         qDebug() << "------------------------------------------------------------------------";
//         kWarning() << "The source" << sourceName << "gets filled with data from multiple threads";
//         qDebug() << "------------------------------------------------------------------------";
//         qDebug() << "------------------------------------------------------------------------";
//     }
    m_publishedData[ sourceName ].clear();
}

void ServiceProviderScript::jobDone( ThreadWeaver::Job* job )
{
    ScriptJob *scriptJob = qobject_cast< ScriptJob* >( job );
    Q_ASSERT( scriptJob );

    const QString sourceName = scriptJob->request()->sourceName;
    PublicTransportInfoList results = m_publishedData.take( sourceName );
    scriptJob->deleteLater();
}

void ServiceProviderScript::jobFailed( ThreadWeaver::Job* job )
{
    ScriptJob *scriptJob = qobject_cast< ScriptJob* >( job );
    Q_ASSERT( scriptJob );

    emit errorParsing( this, ErrorParsingFailed, scriptJob->errorString(),
                       scriptJob->lastDownloadUrl(), scriptJob->request() );
}

void ServiceProviderScript::requestDepartures( const DepartureRequest &request )
{
    if ( !lazyLoadScript() ) {
        kDebug() << "Failed to load script!";
        return;
    }

    DepartureJob *job = new DepartureJob( m_scriptData, request, this );
    connect( job, SIGNAL(started(ThreadWeaver::Job*)), this, SLOT(jobStarted(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(done(ThreadWeaver::Job*)), this, SLOT(jobDone(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(failed(ThreadWeaver::Job*)), this, SLOT(jobFailed(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(departuresReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,DepartureRequest,bool)),
             this, SLOT(departuresReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,DepartureRequest,bool)) );
    ThreadWeaver::Weaver::instance()->enqueue( job );
    return;
}

void ServiceProviderScript::requestArrivals( const ArrivalRequest &request )
{
    if ( !lazyLoadScript() ) {
        kDebug() << "Failed to load script!";
        return;
    }

    ArrivalJob *job = new ArrivalJob( m_scriptData, request, this );
    connect( job, SIGNAL(started(ThreadWeaver::Job*)), this, SLOT(jobStarted(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(done(ThreadWeaver::Job*)), this, SLOT(jobDone(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(failed(ThreadWeaver::Job*)), this, SLOT(jobFailed(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(arrivalsReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,ArrivalRequest,bool)),
             this, SLOT(arrivalsReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,ArrivalRequest,bool)) );
    ThreadWeaver::Weaver::instance()->enqueue( job );
    return;
}

void ServiceProviderScript::requestJourneys( const JourneyRequest &request )
{
    if ( !lazyLoadScript() ) {
        kDebug() << "Failed to load script!";
        return;
    }

    JourneyJob *job = new JourneyJob( m_scriptData, request, this );
    connect( job, SIGNAL(done(ThreadWeaver::Job*)), this, SLOT(jobDone(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(journeysReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,JourneyRequest,bool)),
             this, SLOT(journeysReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,JourneyRequest,bool)) );
    ThreadWeaver::Weaver::instance()->enqueue( job );
    return;
}

void ServiceProviderScript::requestStopSuggestions( const StopSuggestionRequest &request )
{
    if ( !lazyLoadScript() ) {
        kDebug() << "Failed to load script!";
        return;
    }

    StopSuggestionsJob *job = new StopSuggestionsJob( m_scriptData, request, this );
    connect( job, SIGNAL(done(ThreadWeaver::Job*)), this, SLOT(jobDone(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(stopSuggestionsReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,StopSuggestionRequest,bool)),
             this, SLOT(stopSuggestionsReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,StopSuggestionRequest,bool)) );
    ThreadWeaver::Weaver::instance()->enqueue( job );
    return;
}

void ServiceProviderScript::requestStopSuggestionsFromGeoPosition(
        const StopSuggestionFromGeoPositionRequest &request )
{
    if ( !lazyLoadScript() ) {
        kDebug() << "Failed to load script!";
        return;
    }

    StopSuggestionsFromGeoPositionJob *job = new StopSuggestionsFromGeoPositionJob(
            m_scriptData, request, this );
    connect( job, SIGNAL(done(ThreadWeaver::Job*)), this, SLOT(jobDone(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(stopSuggestionsReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,StopSuggestionRequest,bool)),
             this, SLOT(stopSuggestionsReady(QList<TimetableData>,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,StopSuggestionRequest,bool)) );
    ThreadWeaver::Weaver::instance()->enqueue( job );
    return;
}

void ServiceProviderScript::requestAdditionalData( const AdditionalDataRequest &request )
{
    if ( !lazyLoadScript() ) {
        kDebug() << "Failed to load script!";
        return;
    }

    AdditionalDataJob *job = new AdditionalDataJob( m_scriptData, request, this );
    connect( job, SIGNAL(done(ThreadWeaver::Job*)), this, SLOT(jobDone(ThreadWeaver::Job*)) );
    connect( job, SIGNAL(additionalDataReady(TimetableData,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,AdditionalDataRequest,bool)),
             this, SLOT(additionDataReady(TimetableData,ResultObject::Features,ResultObject::Hints,QString,GlobalTimetableInfo,AdditionalDataRequest,bool)) );
    ThreadWeaver::Weaver::instance()->enqueue( job );
    return;
}

void ServiceProviderScript::import( const QString &import, QScriptEngine *engine )
{
    QMutexLocker locker( m_mutex );
    engine->importExtension( import );
}
