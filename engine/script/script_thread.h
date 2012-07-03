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

/** @file
* @brief This file contains a thread which executes service provider plugin scripts.
*
* @author Friedrich Pülz <fpuelz@gmx.de> */

#ifndef SCRIPTTHREAD_HEADER
#define SCRIPTTHREAD_HEADER

// Own includes
#include "enums.h"
#include "scripting.h"
#include "serviceproviderdata.h"

// KDE includes
#include <ThreadWeaver/Job> // Base class

// Qt includes
#include <QScriptEngineAgent> // Base class
#include <QPointer>

struct AbstractRequest;
struct DepartureRequest;
struct ArrivalRequest;
struct StopSuggestionRequest;
struct JourneyRequest;

class ServiceProviderData;
namespace Scripting {
    class Storage;
    class Network;
    class Helper;
    class ResultObject;
};

class QScriptProgram;
class QScriptEngine;

using namespace Scripting;

/** @brief Stores information about a departure/arrival/journey/stop suggestion. */
typedef QHash<TimetableInformation, QVariant> TimetableData;

typedef NetworkRequest* NetworkRequestPtr;
QScriptValue networkRequestToScript( QScriptEngine *engine, const NetworkRequestPtr &request );
void networkRequestFromScript( const QScriptValue &object, NetworkRequestPtr &request );
bool importExtension( QScriptEngine *engine, const QString &extension );

/**
 * @brief A QScriptEngineAgent that signals when a script finishes.
 *
 * After a function exit the agent waits a little bit and checks if the script is still executing
 * using QScriptEngineAgent::isEvaluating().
 **/
class ScriptAgent : public QObject, public QScriptEngineAgent {
    Q_OBJECT

public:
    /** @brief Creates a new ScriptAgent instance. */
    ScriptAgent( QScriptEngine* engine = 0, QObject *parent = 0 );

    /** Overwritten to get noticed when a script might have finished. */
    virtual void functionExit( qint64 scriptId, const QScriptValue& returnValue );

signals:
    /** @brief Emitted, when the script is no longer running */
    void scriptFinished();

protected slots:
    void checkExecution();
};

/** @brief Implements the script function 'importExtension()'. */
bool importExtension( QScriptEngine *engine, const QString &extension );

/**
 * @brief Executes a script.
 *
 * @ingroup scripting
 **/
class ScriptJob : public ThreadWeaver::Job {
    Q_OBJECT

public:
    /**
     * @brief Creates a new ScriptJob.
     *
     * @param script The script to executes.
     * @param data Information about the service provider.
     * @param scriptStorage The shared Storage object.
     **/
    explicit ScriptJob( QScriptProgram *script, const ServiceProviderData *data,
                        Storage *scriptStorage, QObject* parent = 0 );

    /** @brief Destructor. */
    virtual ~ScriptJob();

    /** @brief Return a pointer to the object containing inforamtion about the request of this job. */
    virtual const AbstractRequest* request() const = 0;

    /** @brief Return the number of items which are already published. */
    int publishedItems() const { return m_published; };

    /** @brief Return a string describing the error, if success() returns false. */
    QString errorString() const { return m_errorString; };

    /** @brief TODO. */
    QString lastDownloadUrl() const { return m_lastUrl; };

signals:
    /** @brief Signals ready TimetableData items. */
    void departuresReady( const QList<TimetableData> &departures,
                          ResultObject::Features features, ResultObject::Hints hints,
                          const QString &url, const GlobalTimetableInfo &globalInfo,
                          const DepartureRequest &request, bool couldNeedForcedUpdate = false );

    /** @brief Signals ready TimetableData items. */
    void arrivalsReady( const QList<TimetableData> &arrivals,
                        ResultObject::Features features, ResultObject::Hints hints,
                        const QString &url, const GlobalTimetableInfo &globalInfo,
                        const ArrivalRequest &request, bool couldNeedForcedUpdate = false );

    /** @brief Signals ready TimetableData items. */
    void journeysReady( const QList<TimetableData> &journeys,
                        ResultObject::Features features, ResultObject::Hints hints,
                        const QString &url, const GlobalTimetableInfo &globalInfo,
                        const JourneyRequest &request, bool couldNeedForcedUpdate = false );

    /** @brief Signals ready TimetableData items. */
    void stopSuggestionsReady( const QList<TimetableData> &stops,
                               ResultObject::Features features, ResultObject::Hints hints,
                               const QString &url, const GlobalTimetableInfo &globalInfo,
                               const StopSuggestionRequest &info,
                               bool couldNeedForcedUpdate = false );

protected slots:
    /** @brief Handle the ResultObject::publish() signal by emitting dataReady(). */
    void publish();

protected:
    /** @brief Perform the job. */
    virtual void run();

    /** @brief Load @p script into the engine and insert some objects/functions. */
    bool loadScript( QScriptProgram *script );

    /** @brief Overwritten from ThreadWeaver::Job to return whether or not the job was successful. */
    virtual bool success() const { return m_success; };

    QScriptEngine *m_engine;
    QScriptProgram *m_script;
    Storage *m_scriptStorage;
    QSharedPointer<Network> m_scriptNetwork;
    QSharedPointer<ResultObject> m_scriptResult;

    int m_published;
    bool m_success;
    QString m_errorString;

    ServiceProviderData m_data;
    QString m_lastUrl;
};

class DepartureJobPrivate;
class DepartureJob : public ScriptJob {
    Q_OBJECT

public:
    explicit DepartureJob( QScriptProgram* script, const ServiceProviderData* info,
                           Storage* scriptStorage, const DepartureRequest& request,
                           QObject* parent = 0);

    virtual ~DepartureJob();

    virtual const AbstractRequest* request() const;

private:
    const DepartureJobPrivate *d;
};

class ArrivalJobPrivate;
class ArrivalJob : public ScriptJob {
    Q_OBJECT

public:
    explicit ArrivalJob( QScriptProgram* script, const ServiceProviderData* info,
                         Storage* scriptStorage, const ArrivalRequest& request,
                         QObject* parent = 0);

    virtual ~ArrivalJob();

    virtual const AbstractRequest* request() const;

private:
    const ArrivalJobPrivate *d;
};

class JourneyJobPrivate;
class JourneyJob : public ScriptJob {
    Q_OBJECT

public:
    explicit JourneyJob( QScriptProgram* script, const ServiceProviderData* info,
                         Storage* scriptStorage, const JourneyRequest& request,
                         QObject* parent = 0);

    virtual ~JourneyJob();

    virtual const AbstractRequest* request() const;

private:
    const JourneyJobPrivate *d;
};

class StopSuggestionsJobPrivate;
class StopSuggestionsJob : public ScriptJob {
    Q_OBJECT

public:
    explicit StopSuggestionsJob( QScriptProgram* script, const ServiceProviderData* info,
                                 Storage* scriptStorage, const StopSuggestionRequest& request,
                                 QObject* parent = 0);

    virtual ~StopSuggestionsJob();

    virtual const AbstractRequest* request() const;

private:
    const StopSuggestionsJobPrivate *d;
};

#endif // Multiple inclusion guard
