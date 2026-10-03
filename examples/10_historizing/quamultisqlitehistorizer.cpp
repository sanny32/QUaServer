#include "quamultisqlitehistorizer.h"

#ifdef UA_ENABLE_HISTORIZING

#include <QSqlError>
#include <QSqlRecord>

#include <QFileInfo>
#include <QDir>

// map supported types
QHash<int, QString> QUaMultiSqliteHistorizer::_hashTypes = {
	{QMetaType::Bool           , "INTEGER"},
	{QMetaType::Char           , "INTEGER"},
	{QMetaType::SChar          , "INTEGER"},
	{QMetaType::UChar          , "INTEGER"},
	{QMetaType::Short          , "INTEGER"},
	{QMetaType::UShort         , "INTEGER"},
	{QMetaType::Int            , "INTEGER"},
	{QMetaType::UInt           , "INTEGER"},
	{QMetaType::Long           , "INTEGER"},
	{QMetaType::LongLong       , "INTEGER"},
	{QMetaType::ULong          , "INTEGER"},
	{QMetaType::ULongLong      , "INTEGER"},
	{QMetaType::Float          , "REAL"   },
	{QMetaType::Double         , "REAL"   },
	{QMetaType::QString        , "TEXT"   },
	{QMetaType::QDateTime      , "INTEGER"},
	{QMetaType::QUuid          , "INTEGER"},
	{QMetaType::QByteArray     , "BLOB"   },
	{QMetaType::UnknownType    , "TEXT"   },
	{QMetaType_DataType        , "INTEGER"},
	{QMetaType_NodeId          , "TEXT"   },
	{QMetaType_StatusCode      , "TEXT"   },
	{QMetaType_QualifiedName   , "TEXT"   },
	{QMetaType_LocalizedText   , "TEXT"   },
	{QMetaType_List_NodeId       , "TEXT"   },
	{QMetaType_List_StatusCode   , "TEXT"   },
	{QMetaType_List_QualifiedName, "TEXT"   },
	{QMetaType_List_LocalizedText, "TEXT"   },
	{QMetaType_List_DataType     , "TEXT"   }
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	,
	{QMetaType_TimeZone                    , "TEXT"},
	{QMetaType_ChangeStructureDataType     , "TEXT"},
	{QMetaType_List_ChangeStructureDataType, "TEXT"}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
};

QUaMultiSqliteHistorizer::QUaMultiSqliteHistorizer()
{
	_timeoutTransaction = 1000;
	_fileSizeLimMb      = 1;
	_totalSizeLimMb     = 50;
	_multiRowInsertSize = 20;
	_strBaseName        = "uahist";
	_strSuffix          = "sqlite";
	_deferTotalSizeCheck = false;	
	// handle transation
	QObject::connect(&_timerTransaction, &QTimer::timeout, &_timerTransaction,
	[this]() {
		// stop timer until next write request
		_timerTransaction.stop();
		// get most recent db file
		auto &dbInfo = _dbFiles.last();
		// check if we need to flush row blocks
		if (_multiRowInsertSize > 1)
		{
			bool ok = this->flushOutstandingRowBlocks(
				dbInfo,
				_deferedLogOut
			);
			if (!ok)
			{
				return;
			}
		}
		// commit transaction if not yet commited
		if (dbInfo.openedTransaction)
		{
			// get db
			QSqlDatabase db;
			if (!this->getOpenedDatabase(dbInfo, db, _deferedLogOut))
			{
				return;
			}
			if(!db.commit())
			{
				_deferedLogOut << QUaLog({
					QObject::tr("Failed to commit transaction in %1 database. Sql : %2.")
						.arg(dbInfo.strFileName)
						.arg(db.lastError().text()),
					QUaLogLevel::Error,
					QUaLogCategory::History
				});
			}
			dbInfo.openedTransaction = false;
		}
		// check if need to change database
		bool ok = this->checkDatabase(QDateTime::currentDateTimeUtc(), _deferedLogOut);
		Q_ASSERT(ok);
	}, Qt::QueuedConnection);
	// handle auto close databases
	QObject::connect(&_timerAutoCloseDatabases, &QTimer::timeout, &_timerAutoCloseDatabases,
	[this]() {
		// stop temporarily
		_timerAutoCloseDatabases.stop();
		// check if need to close
		for (auto dbCurr = _dbFiles.begin(); dbCurr != _dbFiles.end(); dbCurr++)
		{
			auto& dbInfo = dbCurr.value();
			// ignore if not opened
			if (
				!QSqlDatabase::contains(dbInfo.strFileName) ||
				dbInfo.autoCloseTimer.elapsed() < _timeoutAutoCloseDatabases
				)
			{
				continue;
			}
			this->closeDatabase(dbInfo, _deferedLogOut);
		}
		// restart if required
		if (_timeoutAutoCloseDatabases <= 0)
		{
			return;
		}
		// check every 1/10 of cycle, minimum check every second
		_timerAutoCloseDatabases.start((std::max)(_timeoutAutoCloseDatabases / 10, 1000));
	}, Qt::QueuedConnection);
	_timeoutAutoCloseDatabases = 5000;
	// check every 1/10 of cycle, minimum check every second
	_timerAutoCloseDatabases.start((std::max)(_timeoutAutoCloseDatabases/10, 1000));
}

QUaMultiSqliteHistorizer::~QUaMultiSqliteHistorizer()
{
	// close all db files
	while (!_dbFiles.isEmpty())
	{
		auto dbInfo = _dbFiles.take(_dbFiles.firstKey());
		this->closeDatabase(dbInfo, _deferedLogOut);
	}
}

QString QUaMultiSqliteHistorizer::databasePath() const
{
	return _strDatabasePath;
}

bool QUaMultiSqliteHistorizer::setDatabasePath(
	const QString& databasePath, 
	QQueue<QUaLog>& logOut
)
{
	// call private method
	return this->setDatabasePath(
		QDateTime::currentDateTimeUtc(),
		databasePath,
		logOut
	);
}

bool QUaMultiSqliteHistorizer::setDatabasePath(
	const QDateTime& startTime,
	const QString& databasePath,
	QQueue<QUaLog>& logOut
)
{
	// stop watcher on old path
	if (!_strDatabasePath.isEmpty())
	{
		_watcher.removePath(_strDatabasePath);
	}
	QObject::disconnect(_fileWatchConn);
	// copy internally
	_strDatabasePath = databasePath.isEmpty() ? "." : databasePath;
	// close all db files
	while (!_dbFiles.isEmpty())
	{
		auto dbInfo = _dbFiles.take(_dbFiles.firstKey());
		bool ok = this->closeDatabase(dbInfo, logOut);
		Q_ASSERT(ok);
		Q_UNUSED(ok);
	}
	// load matching files in new path
	this->reloadMatchingFiles(true, logOut);
	// if no existing file, try to create on target path
	if (_dbFiles.isEmpty())
	{
		bool ok = createNewDatabase(startTime, logOut);
		if (!ok)
		{
			return false;
		}
	}
	// start watching path
	_watchingTimer.restart();
	_watcher.addPath(_strDatabasePath);
    _fileWatchConn = QObject::connect(
    &_watcher, 
    &QFileSystemWatcher::directoryChanged, [this]() {       
		// sqlite journal triggers this signal all the time, so only reload after some time
		if (_watchingTimer.elapsed() < (qint64)(std::max)(_dbFiles.count(), 10) * 200)
		{
			return;
		}
		_watchingTimer.restart();
		// reload, do not warn existing
		this->reloadMatchingFiles(false, _deferedLogOut);
	});
	return true;
}

bool QUaMultiSqliteHistorizer::createNewDatabase(
	const QDateTime& startTime,
	QQueue<QUaLog>& logOut
)
{
	// check target path exists
	if (!QDir(_strDatabasePath).exists())
	{
		logOut << QUaLog({
			QObject::tr("Database target path does not exists. Trying to create. %1.")
				.arg(_strDatabasePath),
			QUaLogLevel::Info,
			QUaLogCategory::History
		});
		bool ok = QDir().mkdir(_strDatabasePath);
		if (!ok)
		{
			logOut << QUaLog({
				QObject::tr("Failed to create database target path. %1.")
					.arg(_strDatabasePath),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			return false;
		}
	}
	// add new one
	auto currDateTime = (std::min)(startTime, QDateTime::currentDateTimeUtc());
	Q_ASSERT(!_dbFiles.contains(currDateTime));
	// NOTE : new is added here
	_dbFiles[currDateTime].strFileName = QString("%1/%2_%3.%4")
		.arg(_strDatabasePath)
		.arg(_strBaseName)
		.arg(currDateTime.toMSecsSinceEpoch())
		.arg(_strSuffix);
	_dbFiles[currDateTime].openedTransaction = false;
	// create and test open database handle
	QSqlDatabase db;
	if (!this->getOpenedDatabase(_dbFiles[currDateTime], db, logOut))
	{
		return false;
	}
	// success
	return true;
}

QUaMultiSqliteHistorizer::DatabaseInfo& 
QUaMultiSqliteHistorizer::getMostRecentDbInfo(
	const QDateTime& startTime,
	bool& ok,
	QQueue<QUaLog>& logOut
)
{
	// get most recent if any exists
	if (!_dbFiles.isEmpty())
	{
		ok = true;
		return _dbFiles.last();
	}	
	// create
	ok = this->setDatabasePath(
		startTime,
		_strDatabasePath, 
		logOut
	);
	if (!ok && _dbFiles.isEmpty())
	{
		_dbFiles[QDateTime()].strFileName = "";
	}
	Q_ASSERT(!_dbFiles.isEmpty());
	return _dbFiles.last();
}

double QUaMultiSqliteHistorizer::fileSizeLimMb() const
{
	return _fileSizeLimMb;
}

void QUaMultiSqliteHistorizer::setFileSizeLimMb(const double& fileSizeLimMb)
{
	if (fileSizeLimMb == _fileSizeLimMb)
	{
		return;
	}
	// low limit
	_fileSizeLimMb = (std::max)(0.0, fileSizeLimMb);
}

double QUaMultiSqliteHistorizer::totalSizeLimMb() const
{
	return _totalSizeLimMb;
}

void QUaMultiSqliteHistorizer::setTotalSizeLimMb(const double& totalSizeLimMb)
{
	if (totalSizeLimMb == _totalSizeLimMb)
	{
		return;
	}
	// low limit
	_totalSizeLimMb = (std::max)(0.0, totalSizeLimMb);
	_totalSizeLimMb = _totalSizeLimMb == 0 ?
		_totalSizeLimMb :
		(std::max)(_totalSizeLimMb, _fileSizeLimMb);
}

int QUaMultiSqliteHistorizer::autoCloseDatabaseTimeout() const
{
	return _timeoutAutoCloseDatabases;
}

void QUaMultiSqliteHistorizer::setAutoCloseDatabaseTimeout(const int& timeoutMs)
{
	if (timeoutMs == _timeoutAutoCloseDatabases)
	{
		return;
	}
	// low limit on transactionTimeout if != 0
	_timeoutAutoCloseDatabases = (std::max)(0, timeoutMs);
	_timeoutAutoCloseDatabases = _timeoutAutoCloseDatabases == 0 ? 
		_timeoutAutoCloseDatabases :
		(std::max)(_timeoutAutoCloseDatabases, _timeoutTransaction);
	// restart timer if necessary
	_timerAutoCloseDatabases.stop();
	if (_timeoutAutoCloseDatabases > 0)
	{		
		// check every 1/10 of cycle, minimum check every second
		_timerAutoCloseDatabases.start((std::max)(_timeoutAutoCloseDatabases / 10, 1000));
	}
}

int QUaMultiSqliteHistorizer::transactionTimeout() const
{
	return _timeoutTransaction;
}

void QUaMultiSqliteHistorizer::setTransactionTimeout(const int& timeoutMs)
{
	_timeoutTransaction = (std::max)(0, timeoutMs);
	if (_timeoutTransaction <= 0)
	{
		_checkingTimer.restart();
	}	
}

int QUaMultiSqliteHistorizer::multiRowInsertSize() const
{
	return _multiRowInsertSize;
}

void QUaMultiSqliteHistorizer::setMultiRowInsertSize(const int& rows)
{
	if (rows == _multiRowInsertSize)
	{
		return;
	}
	// update multirow queries
	_multiRowInsertSize = (std::max)(rows, 1);
	// get most recent db file, creates one of not exist
	bool ok = false;
	auto& dbInfo = this->getMostRecentDbInfo(QDateTime::currentDateTimeUtc(), ok, _deferedLogOut);
	if (!ok)
	{
		return;
	}
	// get database handle
	QSqlDatabase db;
	if (!this->getOpenedDatabase(dbInfo, db, _deferedLogOut))
	{
		return;
	}	
	QString strStmt;
	for (auto& nodeId : dbInfo.dataPrepStmts.keys())
	{
		QSqlQuery query(db);
		// prepared statement for multirow insert
		strStmt = QString(
			"INSERT INTO \"%1\" (Time, Value, Status) VALUES "
		).arg(nodeId);
		for (int i = 0; i < _multiRowInsertSize; i++)
		{
			if (i == _multiRowInsertSize - 1)
			{
				// last one
				strStmt += QString("(:t%1, :v%1, :s%1);").arg(i);
				break;
			}
			strStmt += QString("(:t%1, :v%1, :s%1), ").arg(i);
		}
		if (!this->prepareStmt(dbInfo, query, strStmt, _deferedLogOut))
		{
			continue;
		}
		dbInfo.dataPrepStmts[nodeId].writeHistoryDataMultiRow = query;
	}
}

bool QUaMultiSqliteHistorizer::writeHistoryData(
	const QUaNodeId &nodeId,
	const QUaHistoryDataPoint& dataPoint,
	QQueue<QUaLog>& logOut
)
{
	// check if there are any queued logs that need to be reported
	if (!_deferedLogOut.isEmpty())
	{
		logOut << _deferedLogOut;
		_deferedLogOut.clear();
	}
	// get most recent db file, creates one of not exist
	bool ok = false;
	auto& dbInfo = this->getMostRecentDbInfo(dataPoint.timestamp, ok, logOut);
	if (!ok)
	{
		return false;
	}
	// if transactions disabled, check if need to change database here
	// NOTE : if transactions enabled, this check if performed in transation timeout
	if (_timeoutTransaction <= 0 && _checkingTimer.elapsed() > 5000)
	{
		_checkingTimer.restart();
		bool ok = this->checkDatabase(dataPoint.timestamp, logOut);
		if (!ok)
		{
			return ok;
		}
	}
	// get database handle
	QSqlDatabase db;
	if (!this->getOpenedDatabase(dbInfo, db, logOut))
	{
		return false;
	}
	// handle transactions
	if (!this->handleTransactions(dbInfo, db, logOut))
	{
		return false;
	}
	// check data table exists
	bool dataTableExists;
	if (!this->tableDataByNodeIdExists(dbInfo, db, nodeId, dataTableExists, logOut))
	{
		return false;
	}
	if (!dataTableExists)
	{
		// get sql data type to store
		auto dataType = QUaMultiSqliteHistorizer::QVariantToQtType(dataPoint.value);
		if (!this->createDataNodeTable(dbInfo, db, nodeId, dataType, logOut))
		{
			return false;
		}
	}
	// insert new data point
	return this->insertDataPoint(
		dbInfo,
		db,
		nodeId,
		dataPoint,
		logOut
	);
}

bool QUaMultiSqliteHistorizer::updateHistoryData(
	const QUaNodeId& nodeId,
	const QUaHistoryDataPoint& dataPoint,
	QQueue<QUaLog>& logOut
)
{
	Q_UNUSED(nodeId);
	Q_UNUSED(dataPoint);
	Q_UNUSED(logOut);
	// TODO : implement; left as exercise
	return false;
}

bool QUaMultiSqliteHistorizer::removeHistoryData(
	const QUaNodeId& nodeId,
	const QDateTime& timeStart,
	const QDateTime& timeEnd,
	QQueue<QUaLog>& logOut
)
{
	Q_UNUSED(nodeId);
	Q_UNUSED(timeStart);
	Q_UNUSED(timeEnd);
	Q_UNUSED(logOut);
	// TODO : implement; left as exercise
	return false;
}

QDateTime QUaMultiSqliteHistorizer::firstTimestamp(
	const QUaNodeId& nodeId,
	QQueue<QUaLog>& logOut
)
{
	// look in all db files starting from the first one
	for (auto dbCurr = _dbFiles.begin(); dbCurr != _dbFiles.end(); dbCurr++)
	{
		auto& dbInfo = dbCurr.value();
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check data table exists
		bool dataTableExists;
		if (!this->tableDataByNodeIdExists(dbInfo, db, nodeId, dataTableExists, logOut))
		{
			continue;
		}
		if (!dataTableExists)
		{
			// it is possible that a variable was not yet historized in an old file
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		// get prepared statement cache
		QSqlQuery& query = dbInfo.dataPrepStmts[nodeId].firstTimestamp;
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table for first timestamp in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		if (!query.next())
		{
			// only print warning if no multi row
			if(_multiRowInsertSize <= 1)
			{
				logOut << QUaLog({
					QObject::tr("Empty result querying [%1] table for first timestamp in %2 database. Sql : %3.")
						.arg(nodeId)
						.arg(dbInfo.strFileName)
						.arg(query.lastError().text()),
					QUaLogLevel::Warning,
					QUaLogCategory::History
				});
			}
			// always continue if query fails
			continue;
		}
		// get time key
		QSqlRecord rec = query.record();
		int timeKeyCol = rec.indexOf("Time");
		Q_ASSERT(timeKeyCol >= 0);
		auto timeInt = query.value(timeKeyCol).toLongLong();
		// return
		return QDateTime::fromMSecsSinceEpoch(timeInt, QTimeZone::UTC);
	}
	// check row blocks
	if (_multiRowInsertSize > 1)
	{
		// get most recent db file, creates one of not exist
		bool ok = false;
		auto& dbInfo = this->getMostRecentDbInfo(QDateTime::currentDateTimeUtc(), ok, logOut);
		if (ok && dbInfo.dataPrepStmts.contains(nodeId))
		{
			// check if there is data in the current block
			DataPointBlock& block = dbInfo.dataPrepStmts[nodeId].multiRowBlock;
			if (!block.isEmpty())
			{
				return block.firstKey();
			}
		}		
	}
	// if reached here means we did not find historic data for given nodeId
	logOut << QUaLog({
		QObject::tr("Could not find first timestamp for %1 in database.")
			.arg(nodeId),
		QUaLogLevel::Warning,
		QUaLogCategory::History
	});
	// return invalid
	return QDateTime();
}

QDateTime QUaMultiSqliteHistorizer::lastTimestamp(
	const QUaNodeId& nodeId,
	QQueue<QUaLog>& logOut
)
{
	// look in all db files starting from the last one
	bool exitLoop = false;
	for (auto dbCurr = --_dbFiles.end(); !exitLoop; dbCurr--)
	{
		if (dbCurr == _dbFiles.begin())
		{
			exitLoop = true;
		}
		auto& dbInfo = dbCurr.value();
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check data table exists
		bool dataTableExists;
		if (!this->tableDataByNodeIdExists(dbInfo, db, nodeId, dataTableExists, logOut))
		{
			continue;
		}
		if (!dataTableExists)
		{
			// it is possible that a variable was stopped from being historized in a newer file
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		// get prepared statement cache
		QSqlQuery& query = dbInfo.dataPrepStmts[nodeId].lastTimestamp;
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table for last timestamp in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		if (!query.next())
		{
			// only print warning if no multi row
			if (_multiRowInsertSize <= 1)
			{
				logOut << QUaLog({
					QObject::tr("Empty result querying [%1] table for last timestamp in %2 database. Sql : %3.")
						.arg(nodeId)
						.arg(dbInfo.strFileName)
						.arg(query.lastError().text()),
					QUaLogLevel::Warning,
					QUaLogCategory::History
				});
			}			
			// always continue if query fails
			continue;
		}
		// get time key
		QSqlRecord rec = query.record();
		int timeKeyCol = rec.indexOf("Time");
		Q_ASSERT(timeKeyCol >= 0);
		auto timeInt = query.value(timeKeyCol).toLongLong();
		// return
		return QDateTime::fromMSecsSinceEpoch(timeInt, QTimeZone::UTC);
	}
	// check row blocks
	if (_multiRowInsertSize > 1)
	{
		// get most recent db file, creates one of not exist
		bool ok = false;
		auto& dbInfo = this->getMostRecentDbInfo(QDateTime::currentDateTimeUtc(), ok, logOut);
		if (ok && dbInfo.dataPrepStmts.contains(nodeId))
		{
			// check if there is data in the current block
			DataPointBlock& block = dbInfo.dataPrepStmts[nodeId].multiRowBlock;
			if (!block.isEmpty())
			{
				return block.lastKey();
			}
		}
	}
	// if reached here means we did not find historic data for given nodeId
	logOut << QUaLog({
		QObject::tr("Could not find last timestamp for %1 in database.")
			.arg(nodeId),
		QUaLogLevel::Warning,
		QUaLogCategory::History
	});
	// return invalid
	return QDateTime();
}

bool QUaMultiSqliteHistorizer::hasTimestamp(
	const QUaNodeId& nodeId,
	const QDateTime& timestamp,
	QQueue<QUaLog>& logOut
)
{
	// check row blocks
	if (_multiRowInsertSize > 1)
	{
		// get most recent db file, creates one of not exist
		bool ok = false;
		auto& dbInfo = this->getMostRecentDbInfo(QDateTime::currentDateTimeUtc(), ok, logOut);
		if (ok && dbInfo.dataPrepStmts.contains(nodeId))
		{
			// check if there is data in the current block
			DataPointBlock& block = dbInfo.dataPrepStmts[nodeId].multiRowBlock;
			if (block.contains(timestamp))
			{
				return true;
			}
		}
	}
	// check if in range
	if (timestamp <= _dbFiles.firstKey())
	{
		return timestamp == _dbFiles.firstKey();
	}
	// find database file where it *could* be
	// return the first element in [first,last) which does not compare less than val
	auto iter = std::lower_bound(_dbFiles.keyBegin(), _dbFiles.keyEnd(), timestamp);
	// need the one before, or out of range if lower_bound returns begin
	iter = iter == _dbFiles.keyBegin() ? _dbFiles.keyEnd() : *iter == timestamp ? iter : --iter;
	// if out of range, return invalid
	Q_ASSERT(!(iter == _dbFiles.keyEnd() || !_dbFiles.contains(*iter)));
	if (iter == _dbFiles.keyEnd() || !_dbFiles.contains(*iter))
	{
		// NOTE : this one is error because should have caught it in the first check
		logOut << QUaLog({
			QObject::tr(" Tried to query history database for node id %1 outside available time range.")
				.arg(nodeId),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
		return false;
	}
	// get possible db file
	Q_ASSERT(_dbFiles.contains(*iter));
	auto& dbInfo = _dbFiles[*iter];
	// get database handle
	QSqlDatabase db;
	if (!this->getOpenedDatabase(dbInfo, db, logOut))
	{
		return false;
	}
	// check data table exists
	bool dataTableExists;
	if (!this->tableDataByNodeIdExists(dbInfo, db, nodeId, dataTableExists, logOut))
	{
		return false;
	}
	if (!dataTableExists)
	{
		// it is possible that a variable was not yet historized in an old file
		return false;
	}
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery& query = dbInfo.dataPrepStmts[nodeId].hasTimestamp;
	query.bindValue(0, timestamp.toMSecsSinceEpoch());
	if (!query.exec())
	{
		logOut << QUaLog({
			QObject::tr("Error querying [%1] table for exact timestamp in %2 database. Sql : %3.")
				.arg(nodeId)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
		return false;
	}
	// exists if at least one result
	if (!query.next())
	{
		logOut << QUaLog({
			QObject::tr("Empty result querying [%1] table for exact timestamp in %2 database. Sql : %3.")
				.arg(nodeId)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Warning,
			QUaLogCategory::History
		});
		return false;
	}
	QSqlRecord rec = query.record();
	auto num = query.value(0).toULongLong();
	bool found = num > 0;
	return found;
}

QDateTime QUaMultiSqliteHistorizer::findTimestamp(
	const QUaNodeId& nodeId,
	const QDateTime& timestamp,
	const QUaHistoryBackend::TimeMatch& match,
	QQueue<QUaLog>& logOut
)
{	
	// if ClosestFromBelow look downwards from end until one sample found
	// if ClosestFromAbove look upwards from start until one sample found
	uint requestHash = qHash(nodeId) ^ qHash(timestamp) ^ qHash(static_cast<int>(match));
	// if looking downwards, first check row blocks (because contains the most recent samples)
	if (_multiRowInsertSize > 1 && match == QUaHistoryBackend::TimeMatch::ClosestFromBelow)
	{
		// get most recent db file, creates one of not exist
		bool ok = false;
		auto& dbInfo = this->getMostRecentDbInfo(QDateTime::currentDateTimeUtc(), ok, logOut);
		if (ok && dbInfo.dataPrepStmts.contains(nodeId))
		{
			QDateTime time;
			// check if there is data in the current block
			DataPointBlock& block = dbInfo.dataPrepStmts[nodeId].multiRowBlock;
			// return closest key from below or invalid if out of range
			if (block.contains(timestamp) &&
				block.constFind(timestamp) != block.begin())
			{
				// return previous key if available
				auto iter = block.find(timestamp) - 1;
				time = iter.key();
			}
			else
			{
				// return the first element in [first,last) which does not compare less than val
				auto iter = std::lower_bound(block.keyBegin(), block.keyEnd(), timestamp);
				// need the one before, or out of range if lower_bound returns begin
				iter = iter == block.keyBegin() ? block.keyEnd() : --iter;
				// if out of range, return invalid
				time = iter == block.keyEnd() ? QDateTime() : *iter;
			}
			// only return if found valid
			if (!time.isNull())
			{
				if (
					_findTimestampCache.contains(requestHash) &&
					(std::abs)(time.toMSecsSinceEpoch() - _findTimestampCache[requestHash].toMSecsSinceEpoch()) < 1000
					)
				{
					return _findTimestampCache[requestHash];
				}
				_findTimestampCache[requestHash] = time;
				return time;
			}
		} // if ok
	} // if row blocks
	// loop files
	auto iter = match == QUaHistoryBackend::TimeMatch::ClosestFromBelow ? --_dbFiles.keyEnd() : _dbFiles.keyBegin();
	auto retTimestamp = QDateTime();
	for (bool exitLoop = false; iter != _dbFiles.keyEnd() && !exitLoop; match == QUaHistoryBackend::TimeMatch::ClosestFromBelow ? iter-- : iter++)
	{
		if (iter == _dbFiles.keyBegin() && match == QUaHistoryBackend::TimeMatch::ClosestFromBelow)
		{
			exitLoop = true;
		}
		// get possible db file
		Q_ASSERT(_dbFiles.contains(*iter));
		auto& dbInfo = _dbFiles[*iter];
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check data table exists
		bool dataTableExists;
		if (!this->tableDataByNodeIdExists(dbInfo, db, nodeId, dataTableExists, logOut))
		{
			continue;
		}
		if (!dataTableExists)
		{
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		// get correct query
		QSqlQuery query;
		switch (match)
		{
		case QUaHistoryBackend::TimeMatch::ClosestFromAbove:
		{
			query = dbInfo.dataPrepStmts[nodeId].findTimestampAbove;
		}
		break;
		case QUaHistoryBackend::TimeMatch::ClosestFromBelow:
		{
			query = dbInfo.dataPrepStmts[nodeId].findTimestampBelow;
		}
		break;
		default:
		{
			Q_ASSERT(false);
		}
		break;
		}
		// set reference time
		query.bindValue(0, timestamp.toMSecsSinceEpoch());
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table around timestamp in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		// if there is none continue
		if (!query.next())
		{
			// if finished searching all and no result
			if (
				(match == QUaHistoryBackend::TimeMatch::ClosestFromBelow && iter == _dbFiles.keyBegin()) ||
				 match == QUaHistoryBackend::TimeMatch::ClosestFromAbove && iter == --_dbFiles.keyEnd()
				)
			{
				// if looking upwards, check row blocks last (because contains the most recent samples)
				if (_multiRowInsertSize > 1 && match == QUaHistoryBackend::TimeMatch::ClosestFromAbove)
				{
					// get most recent db file, creates one of not exist
					bool ok = false;
					auto& dbInfo = this->getMostRecentDbInfo(QDateTime::currentDateTimeUtc(), ok, logOut);
					if (ok && dbInfo.dataPrepStmts.contains(nodeId))
					{
						QDateTime time;
						// check if there is data in the current block
						DataPointBlock& block = dbInfo.dataPrepStmts[nodeId].multiRowBlock;
						// return closest key from above or last one if out of range
						if (block.contains(timestamp) &&
							block.constFind(timestamp) + 1 != block.end())
						{
							// return next key if available
							auto iter = block.find(timestamp) + 1;
							time = iter.key();
						}
						else
						{
							// return the first element in [first,last) which compares greater than val
							auto iter = std::upper_bound(block.keyBegin(), block.keyEnd(), timestamp);
							// if out of range, return invalid
							time = iter == block.keyEnd() ? QDateTime() : *iter;
						}
						// only return if found valid
						if (!time.isNull())
						{
							if (
								_findTimestampCache.contains(requestHash) &&
								(std::abs)(time.toMSecsSinceEpoch() - _findTimestampCache[requestHash].toMSecsSinceEpoch()) < 1000
								)
							{
								return _findTimestampCache[requestHash];
							}
							_findTimestampCache[requestHash] = time;
							return time;
						}
					} // if ok
				} // if row blocks
				// last resource are first and last timestamps
				switch (match)
				{
					case QUaHistoryBackend::TimeMatch::ClosestFromAbove:
					{
						auto ts = this->lastTimestamp(nodeId, logOut);
						if (
							_findTimestampCache.contains(requestHash) && 
							(std::abs)(ts.toMSecsSinceEpoch() - _findTimestampCache[requestHash].toMSecsSinceEpoch()) < 1000
							)
						{
							return _findTimestampCache[requestHash];
						}
						_findTimestampCache[requestHash] = ts;
						return ts;
					}
					break;
					case QUaHistoryBackend::TimeMatch::ClosestFromBelow:
					{
						auto ts = this->firstTimestamp(nodeId, logOut);
						if (
							_findTimestampCache.contains(requestHash) &&
							(std::abs)(ts.toMSecsSinceEpoch() - _findTimestampCache[requestHash].toMSecsSinceEpoch()) < 1000
							)
						{
							return _findTimestampCache[requestHash];
						}
						_findTimestampCache[requestHash] = ts;
						return ts;
					}
					break;
					default:
					{
						Q_ASSERT(false);
						return QDateTime();
					}
					break;
				}
				exitLoop = true;
				break;
			}
			continue;
		} // if no result
		// if result, get time key
		QSqlRecord rec = query.record();
		int timeKeyCol = rec.indexOf("Time");
		Q_ASSERT(timeKeyCol >= 0);
		auto timeInt = query.value(timeKeyCol).toLongLong();
		auto currTimestamp = QDateTime::fromMSecsSinceEpoch(timeInt, QTimeZone::UTC);
		Q_ASSERT(
			retTimestamp == QDateTime() ||
			match == QUaHistoryBackend::TimeMatch::ClosestFromAbove ? 
				currTimestamp > retTimestamp :
				currTimestamp < retTimestamp
		);
		// update
		retTimestamp = currTimestamp;
		// result found, exit loop
		break;
	} // for db files
	// return
	if (
		_findTimestampCache.contains(requestHash) &&
		(std::abs)(retTimestamp.toMSecsSinceEpoch() - _findTimestampCache[requestHash].toMSecsSinceEpoch()) < 1000
		)
	{
		return _findTimestampCache[requestHash];
	}
	_findTimestampCache[requestHash] = retTimestamp;
	return retTimestamp;
}

quint64 QUaMultiSqliteHistorizer::numDataPointsInRange(
	const QUaNodeId& nodeId,
	const QDateTime& timeStart,
	const QDateTime& timeEnd,
	QQueue<QUaLog>& logOut)
{
	// find database file where first sample *could* be
	// return the first element in [first,last) which does not compare less than val
	auto iter = std::lower_bound(_dbFiles.keyBegin(), _dbFiles.keyEnd(), timeStart);
	// need the one before, or out of range if lower_bound returns begin
	iter = iter == _dbFiles.keyBegin() || *iter == timeStart ? iter : --iter;
	// if out of range, return invalid
	if (iter == _dbFiles.keyEnd() || !_dbFiles.contains(*iter))
	{
		logOut << QUaLog({
			QObject::tr(" Tried to query history database for node id %1 outside available time range.")
				.arg(nodeId),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
		return 0;
	}
	quint64 count = 0;
	// look in all db files starting from the first one
	for (/*nothing*/; iter != _dbFiles.keyEnd(); iter++)
	{
		// get possible db file
		Q_ASSERT(_dbFiles.contains(*iter));
		auto& dbInfo = _dbFiles[*iter];
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check data table exists
		bool dataTableExists;
		if (!this->tableDataByNodeIdExists(dbInfo, db, nodeId, dataTableExists, logOut))
		{
			continue;
		}
		if (!dataTableExists)
		{
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		QSqlQuery query;
		if (timeEnd.isValid())
		{
			query = dbInfo.dataPrepStmts[nodeId].numDataPointsInRangeEndValid;
			query.bindValue(0, timeStart.toMSecsSinceEpoch());
			query.bindValue(1, timeEnd.toMSecsSinceEpoch());
		}
		else
		{
			query = dbInfo.dataPrepStmts[nodeId].numDataPointsInRangeEndInvalid;
			query.bindValue(0, timeStart.toMSecsSinceEpoch());
		}
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table for number of points in range in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		if (!query.next())
		{
			logOut << QUaLog({
			QObject::tr("Empty result querying [%1] table for number of points in range in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Warning,
				QUaLogCategory::History
			});
			continue;
		}
		// get number of points in range
		QSqlRecord rec = query.record();
		auto num = query.value(0).toULongLong();
		count += num;
	}
	// check row blocks
	if (_multiRowInsertSize > 1)
	{
		// get most recent db file, creates one of not exist
		bool ok = false;
		auto& dbInfo = this->getMostRecentDbInfo(QDateTime::currentDateTimeUtc(), ok, logOut);
		if (ok && dbInfo.dataPrepStmts.contains(nodeId))
		{
			// check if there is data in the current block
			DataPointBlock& block = dbInfo.dataPrepStmts[nodeId].multiRowBlock;
			// add samples from block
			if (!block.isEmpty() && block.contains(timeEnd))
			{
				const auto &startTs = block.contains(timeStart) ? timeStart : block.firstKey();
				quint64 num = static_cast<quint64>(std::distance(
					block.find(startTs),
					timeEnd.isValid() ? block.find(timeEnd) + 1 : block.end()
				));
				count += num;
				Q_ASSERT(num <= block.count());
			}
		}
	}
	// return total
	return count;
}

QVector<QUaHistoryDataPoint> QUaMultiSqliteHistorizer::readHistoryData(
	const QUaNodeId& nodeId,
	const QDateTime& timeStart,
	const quint64& numPointsOffset,
	const quint64& numPointsToRead,
	QQueue<QUaLog>& logOut)
{
	auto points = QVector<QUaHistoryDataPoint>();
	// return the first element in [first,last) which does not compare less than val
	auto iter = std::lower_bound(_dbFiles.keyBegin(), _dbFiles.keyEnd(), timeStart);
	// need the one before, or out of range if lower_bound returns begin
	iter = iter == _dbFiles.keyBegin() || *iter == timeStart ? iter : --iter;
	// if out of range, return invalid
	if (iter == _dbFiles.keyEnd() || !_dbFiles.contains(*iter))
	{
		logOut << QUaLog({
			QObject::tr(" Tried to query history database for node id %1 outside available time range.")
				.arg(nodeId),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
		return points;
	}
	qint64 trueOffset = numPointsOffset;
	// look in all db files starting from the first one
	for (/*nothing*/; iter != _dbFiles.keyEnd(); iter++)
	{
		// get possible db file
		Q_ASSERT(_dbFiles.contains(*iter));
		auto& dbInfo = _dbFiles[*iter];
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check data table exists
		bool dataTableExists;
		if (!this->tableDataByNodeIdExists(dbInfo, db, nodeId, dataTableExists, logOut))
		{
			continue;
		}
		if (!dataTableExists)
		{
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		QSqlQuery query = dbInfo.dataPrepStmts[nodeId].numDataPointsInRangeEndInvalid;
		query.bindValue(0, timeStart.toMSecsSinceEpoch());
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table for number of points in range in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		if (!query.next())
		{
			logOut << QUaLog({
			QObject::tr("Empty result querying [%1] table for number of points in range in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Warning,
				QUaLogCategory::History
			});
			continue;
		}
		// get number of points in range
		QSqlRecord rec = query.record();
		auto num = query.value(0).toULongLong();
		trueOffset -= num;
		// continue if have not reached offset
		if (trueOffset < 0)
		{
			trueOffset += num;			
			Q_ASSERT(trueOffset >= 0);
			break;
		}
	}
	// prealloc memory
	points.reserve(numPointsToRead);
	Q_ASSERT(points.count() == 0);
	// actually read points
	for (/*nothing*/; iter != _dbFiles.keyEnd(); iter++)
	{
		// get possible db file
		Q_ASSERT(_dbFiles.contains(*iter));
		auto& dbInfo = _dbFiles[*iter];
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check data table exists
		bool dataTableExists;
		if (!this->tableDataByNodeIdExists(dbInfo, db, nodeId, dataTableExists, logOut))
		{
			continue;
		}
		if (!dataTableExists)
		{
			continue;
		}
		// query for reading values
		Q_ASSERT(trueOffset >= 0);
		Q_ASSERT(db.isValid() && db.isOpen());
		QSqlQuery query = dbInfo.dataPrepStmts[nodeId].readHistoryData;
		query.bindValue(0, timeStart.toMSecsSinceEpoch());
		query.bindValue(1, numPointsToRead - points.count());
		query.bindValue(2, trueOffset);
		// offset only used in first file query
		trueOffset = 0;
		// exec query
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table for data points in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		qint64 currFileCount = 0;
		while (query.next())
		{
			QSqlRecord rec   = query.record();
			int timeKeyCol   = rec.indexOf("Time");
			int valueKeyCol  = rec.indexOf("Value");
			int statusKeyCol = rec.indexOf("Status");
			Q_ASSERT(timeKeyCol   >= 0);
			Q_ASSERT(valueKeyCol  >= 0);
			Q_ASSERT(statusKeyCol >= 0);
			auto timeInt = query.value(timeKeyCol).toLongLong();
			auto time    = QDateTime::fromMSecsSinceEpoch(timeInt, QTimeZone::UTC); // NOTE : expensive if spec not defined
			auto value   = query.value(valueKeyCol);
			auto status  = query.value(statusKeyCol).toUInt();
			Q_ASSERT(points.isEmpty() || time > points.last().timestamp);
			points << QUaHistoryDataPoint({
				time, value, status
			});
			currFileCount++;
		}
		Q_ASSERT(points.count() <= numPointsToRead);
		if (points.count() == numPointsToRead)
		{
			break;
		}
	}
	// check row blocks
	if (_multiRowInsertSize > 1 && points.count() < numPointsToRead)
	{
		// get most recent db file, creates one of not exist
		bool ok = false;
		auto& dbInfo = this->getMostRecentDbInfo(QDateTime::currentDateTimeUtc(), ok, logOut);
		if (ok && dbInfo.dataPrepStmts.contains(nodeId))
		{
			// check if there is data in the current block
			DataPointBlock& block = dbInfo.dataPrepStmts[nodeId].multiRowBlock;
			if (!block.isEmpty())
			{
				Q_ASSERT(timeStart <= block.firstKey());
				auto iter = block.begin();
				while (points.count() < numPointsToRead && iter != block.end())
				{
					const auto &ts = iter.key();
					const auto &pt = iter.value();
					points << QUaHistoryDataPoint({
						ts,
						pt.value,
						pt.status
					});
					iter++;
				}
			}
		}
	}
	// NOTE : return an invalid value if API requests more values than available
	Q_ASSERT(points.count() == numPointsToRead);
	while (points.count() < numPointsToRead)
	{
		points << QUaHistoryDataPoint();
	}
	// return
	return points;
}

// event history support
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS

bool QUaMultiSqliteHistorizer::writeHistoryEventsOfType(
	const QUaNodeId& eventTypeNodeId,
	const QList<QUaNodeId>& emittersNodeIds,
	const QUaHistoryEventPoint& eventPoint,
	QQueue<QUaLog>& logOut
)
{
	// check if there are any queued logs that need to be reported
	if (!_deferedLogOut.isEmpty())
	{
		logOut << _deferedLogOut;
		_deferedLogOut.clear();
	}
	// get most recent db file, creates one of not exist
	bool ok = false;
	auto& dbInfo = this->getMostRecentDbInfo(eventPoint.timestamp, ok, logOut);
	if (!ok)
	{
		return false;
	}
	// if transactions disabled, check if need to change database here
	// NOTE : if transactions enabled, this check if performed in transation timeout
	if (_timeoutTransaction <= 0 && _checkingTimer.elapsed() > 5000)
	{
		_checkingTimer.restart();
		bool ok = this->checkDatabase(eventPoint.timestamp, logOut);
		if (!ok)
		{
			return ok;
		}
	}
	// get database handle
	QSqlDatabase db;
	if (!this->getOpenedDatabase(dbInfo, db, logOut))
	{
		return false;
	}
	// handle transactions
	if (!this->handleTransactions(dbInfo, db, logOut))
	{
		return false;
	}
	// check event data table exists
	bool eventTypeTableExists;
	if (!this->tableEventTypeByNodeIdExists(
		dbInfo,
		db,
		eventTypeNodeId,
		eventPoint,
		eventTypeTableExists,
		logOut))
	{
		return false;
	}
	if (!eventTypeTableExists)
	{
		// create event data table
		if (!this->createEventTypeTable(dbInfo, db, eventTypeNodeId, eventPoint, logOut))
		{
			return false;
		}
	}
	// check table of event type names exist
	bool eventTypeNameTableExists;
	if (!this->tableEventTypeNameExists(dbInfo, db, eventTypeNameTableExists, logOut))
	{
		return false;
	}
	if (!eventTypeNameTableExists)
	{
		// create event type name table
		if (!this->createEventTypeNameTable(dbInfo, db, logOut))
		{
			return false;
		}
	}
	// chech each emitter table exists
	for (auto& emitterNodeId : emittersNodeIds)
	{
		bool emitterTableExists;
		if (!this->tableEmitterByNodeIdExists(dbInfo, db, emitterNodeId, emitterTableExists, logOut))
		{
			return false;
		}
		if (!emitterTableExists)
		{
			// create emitter table
			if (!this->createEmitterTable(dbInfo, db, emitterNodeId, logOut))
			{
				return false;
			}
		}
	}
	// insert new event in its event type table, return new event key
	qint64 eventKey = -1;
	if (!this->insertEventPoint(dbInfo, db, eventTypeNodeId, eventPoint, eventKey, logOut))
	{
		return false;
	}
	// check if event type table name already in table else insert it, fetch event type name key
	qint64 outEventTypeKey = -1;
	if (!this->selectOrInsertEventTypeName(dbInfo, db, eventTypeNodeId, outEventTypeKey, logOut))
	{
		return false;
	}
	// insert reference to new event and event type in each emitter
	ok = true;
	for (auto& emitterNodeId : emittersNodeIds)
	{
		ok = ok && this->insertEventReferenceInEmitterTable(
			dbInfo,
			db,
			emitterNodeId,
			eventPoint.timestamp,
			outEventTypeKey,
			eventKey,
			logOut
		);
	}
	return ok;
}

QVector<QUaNodeId> QUaMultiSqliteHistorizer::eventTypesOfEmitter(
	const QUaNodeId& emitterNodeId,
	QQueue<QUaLog>& logOut
)
{
	QVector<QUaNodeId> retTypes;
	// for all db files
	for (auto dbCurr = _dbFiles.begin(); dbCurr != _dbFiles.end(); dbCurr++)
	{
		auto& dbInfo = dbCurr.value();
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		// check table of event type names exist
		bool eventTypeNameTableExists;
		if (!this->tableEventTypeNameExists(dbInfo, db, eventTypeNameTableExists, logOut))
		{
			continue;
		}
		bool emitterTableExists;
		if (!this->tableEmitterByNodeIdExists(dbInfo, db, emitterNodeId, emitterTableExists, logOut))
		{
			continue;
		}
		if (!eventTypeNameTableExists ||
			!emitterTableExists)
		{
			continue;
		}
		// create query
		QSqlQuery query(db);
		QString strStmt = QString(
			"SELECT n.TableName FROM EventTypeTableNames n "
			"INNER JOIN "
			"("
			"SELECT DISTINCT EventType FROM \"%1\""
			") e "
			"ON n.EventTypeTableNames = e.EventType"
		).arg(emitterNodeId);
		// execute query
		if (!query.exec(strStmt))
		{
			logOut << QUaLog({
				QObject::tr("Could not fetch any event types for emitter [%1] in database %2. Sql : %3.")
					.arg(emitterNodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		// loop results
		// NOTE : cannot prealloc retTypes size because QSqlQuery::size for sqlite returns -1
		while (query.next())
		{
			retTypes << query.value(0).toString();
		}
	}
	return retTypes;
}

QDateTime QUaMultiSqliteHistorizer::findTimestampEventOfType(
	const QUaNodeId& emitterNodeId,
	const QUaNodeId& eventTypeNodeId,
	const QDateTime& timestamp,
	const QUaHistoryBackend::TimeMatch& match,
	QQueue<QUaLog>& logOut
)
{
	// if ClosestFromBelow look downwards from end until one sample found
	// if ClosestFromAbove look upwards from start until one sample found
	auto iter = match == QUaHistoryBackend::TimeMatch::ClosestFromBelow ? --_dbFiles.keyEnd() : _dbFiles.keyBegin();
	auto retTimestamp = QDateTime();
	for (bool exitLoop = false; iter != _dbFiles.keyEnd() && !exitLoop; match == QUaHistoryBackend::TimeMatch::ClosestFromBelow ? iter-- : iter++)
	{
		if (iter == _dbFiles.keyBegin() && match == QUaHistoryBackend::TimeMatch::ClosestFromBelow)
		{
			exitLoop = true;
		}
		// get possible db file
		Q_ASSERT(_dbFiles.contains(*iter));
		auto& dbInfo = _dbFiles[*iter];
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check tables exist
		bool eventTypeNameTableExists;
		if (!this->tableEventTypeNameExists(dbInfo, db, eventTypeNameTableExists, logOut))
		{
			continue;
		}
		bool eventTypeTableExists;
		if (!this->tableEventTypeByNodeIdExists(
			dbInfo,
			db,
			eventTypeNodeId,
			QUaHistoryEventPoint(),
			eventTypeTableExists,
			logOut))
		{
			continue;
		}
		bool emitterTableExists;
		if (!this->tableEmitterByNodeIdExists(dbInfo, db, emitterNodeId, emitterTableExists, logOut))
		{
			continue;
		}
		if (!eventTypeNameTableExists || !eventTypeTableExists || !emitterTableExists)
		{
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		// check if event type table name already in table else insert it, fetch event type name key
		qint64 outEventTypeKey = -1;
		if (!this->selectOrInsertEventTypeName(dbInfo, db, eventTypeNodeId, outEventTypeKey, logOut))
		{
			continue;
		}
		Q_ASSERT(outEventTypeKey >= 0);
		// get correct query
		QSqlQuery query(db);
		switch (match)
		{
		case QUaHistoryBackend::TimeMatch::ClosestFromAbove:
		{
			// prepared statement for find timestamp from above
			QString strStmt = QString(
				"SELECT "
				"e.Time "
				"FROM "
				"\"%1\" e "
				"WHERE "
				"e.Time >= :Time "
				"AND "
				"e.EventType = %2 "
				"ORDER BY "
				"e.Time ASC "
				"LIMIT "
				"1;"
			).arg(emitterNodeId).arg(outEventTypeKey);
			if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
			{
				continue;
			}
		}
		break;
		case QUaHistoryBackend::TimeMatch::ClosestFromBelow:
		{
			// prepared statement for find timestamp from below
			QString strStmt = QString(
				"SELECT "
				"e.Time "
				"FROM "
				"\"%1\" e "
				"WHERE "
				"e.Time < :Time "
				"AND "
				"e.EventType = %2 "
				"ORDER BY "
				"e.Time DESC "
				"LIMIT "
				"1;"
			).arg(emitterNodeId).arg(outEventTypeKey);
			if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
			{
				continue;
			}
		}
		break;
		default:
		{
			Q_ASSERT(false);
		}
		break;
		}
		// set reference time
		query.bindValue(0, timestamp.toMSecsSinceEpoch());
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] event emitter table around timestamp in %2 database. Sql : %3.")
					.arg(emitterNodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		// if there is none, try next
		if (!query.next())
		{
			continue;
		}
		// get time key
		QSqlRecord rec = query.record();
		int timeKeyCol = rec.indexOf("Time");
		Q_ASSERT(timeKeyCol >= 0);
		auto timeInt = query.value(timeKeyCol).toLongLong();
		auto currTimestamp = QDateTime::fromMSecsSinceEpoch(timeInt, QTimeZone::UTC);
		Q_ASSERT(
			retTimestamp == QDateTime() ||
			match == QUaHistoryBackend::TimeMatch::ClosestFromAbove ?
			currTimestamp > retTimestamp :
		currTimestamp < retTimestamp
			);
		// update
		retTimestamp = currTimestamp;
		break;
	}
	return retTimestamp;
}

quint64 QUaMultiSqliteHistorizer::numEventsOfTypeInRange(
	const QUaNodeId& emitterNodeId,
	const QUaNodeId& eventTypeNodeId,
	const QDateTime& timeStart,
	const QDateTime& timeEnd,
	QQueue<QUaLog>& logOut
)
{
	// find database file where first sample *could* be
	// return the first element in [first,last) which does not compare less than val
	auto iter = std::lower_bound(_dbFiles.keyBegin(), _dbFiles.keyEnd(), timeStart);
	// need the one before, or out of range if lower_bound returns begin
	iter = iter == _dbFiles.keyBegin() || *iter == timeStart ? iter : --iter;
	// if out of range, return invalid
	if (iter == _dbFiles.keyEnd() || !_dbFiles.contains(*iter))
	{
		logOut << QUaLog({
			QObject::tr(" Tried to query event history database for emitter %1, event type %2 outside available time range.")
				.arg(emitterNodeId).arg(eventTypeNodeId),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
		return 0;
	}
	quint64 count = 0;
	// look in all db files starting from the first one
	for (/*nothing*/; iter != _dbFiles.keyEnd(); iter++)
	{
		// get possible db file
		Q_ASSERT(_dbFiles.contains(*iter));
		auto& dbInfo = _dbFiles[*iter];
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check tables exist
		bool eventTypeTableExists;
		if (!this->tableEventTypeByNodeIdExists(
			dbInfo,
			db,
			eventTypeNodeId,
			QUaHistoryEventPoint(),
			eventTypeTableExists,
			logOut))
		{
			continue;
		}
		if (!eventTypeTableExists)
		{
			continue;
		}
		bool emitterTableExists;
		if (!this->tableEmitterByNodeIdExists(dbInfo, db, emitterNodeId, emitterTableExists, logOut))
		{
			continue;
		}
		if (!emitterTableExists)
		{
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		// check if event type table name already in table else insert it, fetch event type name key
		qint64 outEventTypeKey = -1;
		if (!this->selectOrInsertEventTypeName(dbInfo, db, eventTypeNodeId, outEventTypeKey, logOut))
		{
			continue;
		}
		Q_ASSERT(outEventTypeKey >= 0);
		Q_ASSERT(timeStart.isValid() && timeEnd.isValid());
		QSqlQuery query(db);
		// prepared statement for num points in range
		QString strStmt = QString(
			"SELECT "
			"COUNT(*) "
			"FROM "
			"\"%1\" e "
			"WHERE "
			"e.Time >= :TimeStart "
			"AND "
			"e.Time <= :TimeEnd "
			"AND "
			"e.EventType = %2 "
			"ORDER BY "
			"e.Time ASC;"
		).arg(emitterNodeId).arg(outEventTypeKey);
		if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
		{
			continue;
		}
		// bind
		query.bindValue(":TimeStart", timeStart.toMSecsSinceEpoch());
		query.bindValue(":TimeEnd", timeEnd.toMSecsSinceEpoch());
		// execute
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table for number of events in range in %2 database. Sql : %3.")
					.arg(emitterNodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		if (!query.next())
		{
			logOut << QUaLog({
			QObject::tr("Empty result querying [%1] table for events of events in range in %2 database. Sql : %3.")
					.arg(emitterNodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Warning,
				QUaLogCategory::History
			});
			continue;
		}
		// get number of points in range
		QSqlRecord rec = query.record();
		auto num = query.value(0).toULongLong();	
		count += num;
	}
	// return total
	return count;
}

QVector<QUaHistoryEventPoint> QUaMultiSqliteHistorizer::readHistoryEventsOfType(
	const QUaNodeId& emitterNodeId,
	const QUaNodeId& eventTypeNodeId,
	const QDateTime& timeStart,
	const quint64& numPointsOffset,
	const quint64& numPointsToRead,
	const QList<QUaBrowsePath>& columnsToRead,
	QQueue<QUaLog>& logOut
)
{
	auto points = QVector<QUaHistoryEventPoint>();
	// return the first element in [first,last) which does not compare less than val
	auto iter = std::lower_bound(_dbFiles.keyBegin(), _dbFiles.keyEnd(), timeStart);
	// need the one before, or out of range if lower_bound returns begin
	iter = iter == _dbFiles.keyBegin() || *iter == timeStart ? iter : --iter;
	// if out of range, return invalid
	if (iter == _dbFiles.keyEnd() || !_dbFiles.contains(*iter))
	{
		logOut << QUaLog({
			QObject::tr(" Tried to query history database for emitter %1, event type %2 outside available time range.")
				.arg(emitterNodeId).arg(eventTypeNodeId),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return points;
	}
	qint64 trueOffset = numPointsOffset;
	// look in all db files starting from the first one
	for (/*nothing*/; iter != _dbFiles.keyEnd(); iter++)
	{
		// get possible db file
		Q_ASSERT(_dbFiles.contains(*iter));
		auto& dbInfo = _dbFiles[*iter];
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check tables exist
		bool eventTypeNameTableExists;
		if (!this->tableEventTypeNameExists(dbInfo, db, eventTypeNameTableExists, logOut))
		{
			continue;
		}
		bool eventTypeTableExists;
		if (!this->tableEventTypeByNodeIdExists(
			dbInfo,
			db,
			eventTypeNodeId,
			QUaHistoryEventPoint(),
			eventTypeTableExists,
			logOut))
		{
			continue;
		}
		bool emitterTableExists;
		if (!this->tableEmitterByNodeIdExists(dbInfo, db, emitterNodeId, emitterTableExists, logOut))
		{
			continue;
		}
		if (!eventTypeNameTableExists || !eventTypeTableExists || !emitterTableExists)
		{
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		// check if event type table name already in table else insert it, fetch event type name key
		qint64 outEventTypeKey = -1;
		if (!this->selectOrInsertEventTypeName(dbInfo, db, eventTypeNodeId, outEventTypeKey, logOut))
		{
			continue;
		}
		Q_ASSERT(outEventTypeKey >= 0);
		QSqlQuery query(db);
		// prepared statement for num points in range
		QString strStmt = QString(
			"SELECT "
			"COUNT(*) "
			"FROM "
			"\"%1\" e "
			"WHERE "
			"e.Time >= :TimeStart "
			"AND "
			"e.EventType = %2 "
			"ORDER BY "
			"e.Time ASC;"
		).arg(emitterNodeId).arg(outEventTypeKey);
		if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
		{
			continue;
		}
		// bind
		query.bindValue(":TimeStart", timeStart.toMSecsSinceEpoch());
		// execute
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table for number of events in range in %2 database. Sql : %3.")
					.arg(emitterNodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		if (!query.next())
		{
			logOut << QUaLog({
			QObject::tr("Empty result querying [%1] table for events of events in range in %2 database. Sql : %3.")
					.arg(emitterNodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Warning,
				QUaLogCategory::History
			});
			continue;
		}
		// get number of points in range
		QSqlRecord rec = query.record();
		auto num = query.value(0).toULongLong();
		trueOffset -= num;
		// continue if have not reached offset
		if (trueOffset < 0)
		{
			trueOffset += num;
			Q_ASSERT(trueOffset >= 0);
			break;
		}
	}
	// prealloc size
	points.resize(numPointsToRead);
	quint64 totalNumPointsToRead = 0;
	for (/*nothing*/; iter != _dbFiles.keyEnd(); iter++)
	{
		// get possible db file
		Q_ASSERT(_dbFiles.contains(*iter));
		auto& dbInfo = _dbFiles[*iter];
		// get database handle
		QSqlDatabase db;
		if (!this->getOpenedDatabase(dbInfo, db, logOut))
		{
			continue;
		}
		// check tables exist
		bool eventTypeNameTableExists;
		if (!this->tableEventTypeNameExists(dbInfo, db, eventTypeNameTableExists, logOut))
		{
			continue;
		}
		bool eventTypeTableExists;
		if (!this->tableEventTypeByNodeIdExists(
			dbInfo,
			db,
			eventTypeNodeId,
			QUaHistoryEventPoint(),
			eventTypeTableExists,
			logOut))
		{
			continue;
		}
		bool emitterTableExists;
		if (!this->tableEmitterByNodeIdExists(dbInfo, db, emitterNodeId, emitterTableExists, logOut))
		{
			continue;
		}
		if (!eventTypeNameTableExists || !eventTypeTableExists || !emitterTableExists)
		{
			continue;
		}
		Q_ASSERT(db.isValid() && db.isOpen());
		// check if event type table name already in table else insert it, fetch event type name key
		qint64 outEventTypeKey = -1;
		if (!this->selectOrInsertEventTypeName(dbInfo, db, eventTypeNodeId, outEventTypeKey, logOut))
		{
			continue;
		}
		Q_ASSERT(outEventTypeKey >= 0);
		Q_ASSERT(timeStart.isValid());
		QSqlQuery query(db);

		// get column names
		QString strStmt = QString(
			"SELECT c.name FROM pragma_table_info(\"%1\") c;"
		).arg(eventTypeNodeId);
		if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
		{
			continue;
		}
		// execute
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table for column names in %2 database. Sql : %3.")
					.arg(eventTypeNodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		// read column names results
		QSet<QString> tableCols;
		while (query.next())
		{
			tableCols << query.value(0).toString();
		}
		// get requested column names to read
		QHash<QString, int> colsToIndexes;
		QHash<QString, const QUaBrowsePath*> colsToPaths;
		QString strColumns;
		auto iCol = columnsToRead.begin();
		while (iCol != columnsToRead.end())
		{
			QString strColName = QUaQualifiedName::reduceName(*iCol, "_");
			// ignore if eventid or if requested column does not exist in table
			if (strColName == QLatin1String("EventId") ||
				!tableCols.contains(strColName))
			{
				++iCol;
				continue;
			}
			colsToIndexes[strColName] = -1;
			colsToPaths[strColName] = &(*iCol);
			strColumns += strColName;
			if (++iCol == columnsToRead.end())
			{
				continue;
			}
			strColumns += ", ";
		}
		// prepared statement for num points in range
		strStmt = QString(
			"SELECT %4 FROM \"%1\" t "
			"INNER JOIN ("
			"SELECT "
			"EventId "
			"FROM "
			"\"%2\" e "
			"WHERE "
			"e.Time >= :TimeStart "
			"AND "
			"e.EventType = %3 "
			"ORDER BY "
			"e.Time ASC LIMIT :Limit OFFSET :Offset"
			") e "
			"ON t.\"%1\" = e.EventId;"
		).arg(eventTypeNodeId).arg(emitterNodeId).arg(outEventTypeKey).arg(strColumns);
		if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
		{
			continue;
		}
		// bind
		query.bindValue(":TimeStart", timeStart.toMSecsSinceEpoch());
		query.bindValue(":Limit", numPointsToRead - totalNumPointsToRead);
		query.bindValue(":Offset", trueOffset);
		// offset only used in first file query
		trueOffset = 0;
		// execute
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Error querying [%1] table for event points in %2 database. Sql : %3.")
					.arg(eventTypeNodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			continue;
		}
		// get column indexes
		auto i = colsToIndexes.begin();
		while (i != colsToIndexes.end())
		{
			auto& name = i.key();
			auto& index = i.value();
			index = query.record().indexOf(name);
			Q_ASSERT(index >= 0);
			++i;
		}
		// read row results	
		static const QString strTimeColName("Time");
		while (query.next() && totalNumPointsToRead < numPointsToRead)
		{
			qulonglong iTime = query.value(colsToIndexes[strTimeColName]).toULongLong();
			// NOTE : expensive if time zone (QTimeZone::UTC) not defined
			points[totalNumPointsToRead].timestamp = QDateTime::fromMSecsSinceEpoch(iTime, QTimeZone::UTC);
			auto& fields = points[totalNumPointsToRead].fields;
			// populate fields
			i = colsToIndexes.begin();
			while (i != colsToIndexes.end())
			{
				auto& name = i.key();
				auto& index = i.value();
				// expand browse path from string
				fields.insert(
					*colsToPaths[name],
					query.value(index)
				);
				++i;
			}
			// next row
			totalNumPointsToRead++;
		}
		Q_ASSERT(totalNumPointsToRead <= numPointsToRead);
		if (totalNumPointsToRead == numPointsToRead)
		{
			break;
		}
	}
	// NOTE : return an invalid value if API requests more values than available
	Q_ASSERT(totalNumPointsToRead == numPointsToRead);
	// return
	return points;
}

#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

bool QUaMultiSqliteHistorizer::getOpenedDatabase(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	QQueue<QUaLog>& logOut
)
{
	const QString& strDbName = dbInfo.strFileName;
	// add if not added
	if (QSqlDatabase::contains(strDbName))
	{
		db = QSqlDatabase::database(strDbName, true);
	}
	else
	{
		db = QSqlDatabase::addDatabase("QSQLITE", strDbName);
		// the database name is not the connection name
		db.setDatabaseName(strDbName);
		db.open();
	}
	// check if opened correctly
	if (!db.isOpen())
	{
		logOut << QUaLog({
			QObject::tr("Error opening %1. Sql : %2")
				.arg(strDbName)
				.arg(db.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// restart auto close timer
	dbInfo.autoCloseTimer.restart();
	// success
	return true;
}

bool QUaMultiSqliteHistorizer::closeDatabase(
	DatabaseInfo& dbInfo,
	QQueue<QUaLog>& logOut
)
{
	const QString& strDbName = dbInfo.strFileName;
	// check if there are any outstanding row blocks
	// NOTE : we must flush the blocks because they "live" in the dbInfo.dataPrepStmts
	//        in the DataPreparedStatements struct, so we gotta flush before we clear
	if (_multiRowInsertSize > 1)
	{
		bool ok = this->flushOutstandingRowBlocks(
			dbInfo,
			logOut
		);
		if (!ok)
		{
			return false;
		}		
	}
	// clear queries
	dbInfo.dataPrepStmts.clear();
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	dbInfo.eventTypePrepStmts.clear();
	dbInfo.eventTypeNamePrepStmt.clear();
	dbInfo.emitterPrepStmts.clear();
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
	// close database only if previously opened
	if (!QSqlDatabase::contains(strDbName))
	{
		return true;
	}
	QSqlDatabase::database(strDbName).close();
	QSqlDatabase::removeDatabase(strDbName);
	return true;
}

bool QUaMultiSqliteHistorizer::flushOutstandingRowBlocks(
	DatabaseInfo& dbInfo, 
	QQueue<QUaLog>& logOut
)
{
	Q_ASSERT(_multiRowInsertSize > 1);
	// get database handle
	QSqlDatabase db;
	if (!this->getOpenedDatabase(dbInfo, db, logOut))
	{
		return false;
	}
	// open new transaction if required
	if (!dbInfo.openedTransaction && !db.transaction())
	{
		dbInfo.openedTransaction = false;
		logOut << QUaLog({
			QObject::tr("Could not flush row block. Failed to begin transaction in %1 database. Sql : %2.")
				.arg(dbInfo.strFileName)
				.arg(db.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
		return false;
	}
	dbInfo.openedTransaction = true;
	// loop blocks
	bool ok = true;
	for (auto& nodeId : dbInfo.dataPrepStmts.keys())
	{
		Q_ASSERT(dbInfo.dataPrepStmts.contains(nodeId));
		// check if there is data in the current block
		DataPointBlock& block = dbInfo.dataPrepStmts[nodeId].multiRowBlock;
		int blockSize = block.size();
		if (blockSize <= 0)
		{
			continue;
		}
		// create flush query
		QString strStmt;	
		QSqlQuery query(db);	
		// prepared statement for multirow insert
		strStmt = QString(
			"INSERT INTO \"%1\" (Time, Value, Status) VALUES "
		).arg(nodeId);
		for (int i = 0; i < blockSize; i++)
		{
			if (i == blockSize - 1)
			{
				// last one
				strStmt += QString("(:t%1, :v%1, :s%1);").arg(i);
				break;
			}
			strStmt += QString("(:t%1, :v%1, :s%1), ").arg(i);
		}
		if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
		{
			ok = false;
			continue;
		}
		// flush current block
		for (int i = 0; i < blockSize; i++)
		{
			auto currTime = block.firstKey();
			auto currPoint = block.take(currTime);
			query.bindValue(3 * i, currTime.toMSecsSinceEpoch());
			query.bindValue(3 * i + 1, currPoint.value);
			query.bindValue(3 * i + 2, currPoint.status);
		}
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Could not flush row block in %1 table in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(dbInfo.strFileName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			ok = false;
			continue;
		}
		Q_ASSERT(block.isEmpty());
	}
	if (dbInfo.openedTransaction && !db.commit())
	{
		logOut << QUaLog({
			QObject::tr("Error flushing row block. Failed to commit transaction in %1 database. Sql : %2.")
				.arg(dbInfo.strFileName)
				.arg(db.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
	}
	dbInfo.openedTransaction = false;
	// return "ok" which must be true of all went well
	return ok;
}

bool QUaMultiSqliteHistorizer::checkDatabase(
	const QDateTime& startTime,
	QQueue<QUaLog>& logOut
)
{
	// check if enabled
	if (_fileSizeLimMb <= 0.0)
	{
		return true;
	}
	double fileSizeMb = 0.0;
	double totalSizeMb = 0.0;
	// get most recent db file, creates one of not exist
	bool ok = false;
	auto& dbInfo = this->getMostRecentDbInfo(startTime, ok, logOut);
	if (!ok)
	{
		return false;
	}
	const QString& strDbName = dbInfo.strFileName;
	// check file exists
	QFileInfo fileInfo(strDbName);
	if (!fileInfo.exists())
	{
		logOut << QUaLog({
			QObject::tr("Could check %1 database size. Database file not found.")
				.arg(strDbName),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// convert bytes to Mb
	fileSizeMb = fileInfo.size() / 1024.0 / 1024.0;
	// return ok if size is below limit
	bool totalSizeCheck = false;
	if (fileSizeMb >= _fileSizeLimMb)
	{
		// close current database and create new one
		bool ok = this->createNewDatabase(startTime, logOut);
		if (!ok)
		{
			return false;
		}
		// continue if good
		totalSizeCheck = true;
	}
	// clear this cache once in a while
	_findTimestampCache.clear();
	// check total size
	if (
		_totalSizeLimMb <= 0.0 || // ignore total size if disabled
		(!totalSizeCheck && !_deferTotalSizeCheck)
		)
	{
		return true;
	}
	// calculate total size
	for (auto & dbInfo : _dbFiles)
	{
		auto fInfo = QFileInfo(dbInfo.strFileName);
		totalSizeMb += fInfo.size() / 1024.0 / 1024.0;
	}
	// remove oldest files until total size is ok
	_deferTotalSizeCheck = false;
	while (totalSizeMb > _totalSizeLimMb)
	{
		auto &dbInfo = _dbFiles[_dbFiles.firstKey()];
		// defer remove if db was recently opened for a read
		if (
			QSqlDatabase::contains(dbInfo.strFileName) &&
			_timeoutAutoCloseDatabases > 0 &&
			dbInfo.autoCloseTimer.elapsed() < _timeoutAutoCloseDatabases
			)
		{
			logOut << QUaLog({
				QObject::tr("History file could not be deleted because is in use. %1.")
					.arg(dbInfo.strFileName),
				QUaLogLevel::Warning,
				QUaLogCategory::History
			});
			_deferTotalSizeCheck = true;
			break;
		}
		// close if necessary
		bool ok = this->closeDatabase(dbInfo, logOut);
		if (!ok)
		{
			logOut << QUaLog({
				QObject::tr("History file could not be deleted from file system. Failed to close %1.")
					.arg(dbInfo.strFileName),
				QUaLogLevel::Error,
				QUaLogCategory::History
				});
			_deferTotalSizeCheck = true;
			break;
		}
		// substract size
		auto fInfo = QFileInfo(dbInfo.strFileName);		
		totalSizeMb -= fInfo.size() / 1024.0 / 1024.0;
		// remove file
		QFile file(dbInfo.strFileName);
		ok = file.remove();
		if (!ok)
		{
			logOut << QUaLog({
				QObject::tr("History file could not be deleted from file system. Failed to remove %1.")
					.arg(dbInfo.strFileName),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			_deferTotalSizeCheck = true;
			break;
		}
		// if deleted success, remove from list
		auto dbInfoRemoved = _dbFiles.take(_dbFiles.firstKey());
		logOut << QUaLog({
			QObject::tr("History file deleted due to maximum size restrictions. %1.")
				.arg(dbInfoRemoved.strFileName),
			QUaLogLevel::Warning,
			QUaLogCategory::History
		});
	}
	return true;
}

bool QUaMultiSqliteHistorizer::reloadMatchingFiles(
	const bool& warnToLog,
	QQueue<QUaLog>& logOut
)
{
	auto newDir = QDir(_strDatabasePath);
	if (!newDir.exists())
	{
		if (warnToLog)
		{
			logOut << QUaLog({
				QObject::tr("Non existing directory %1.")
					.arg(_strDatabasePath),
				QUaLogLevel::Warning,
				QUaLogCategory::History
			});
		}
		return false;
	}
	// get list of existing (to check if one deleted)
	auto oldFileSet = _dbFiles.keys().toSet();
	// filter matching files
	newDir.setNameFilters(QStringList() << QString("%1_*.%2").arg(_strBaseName).arg(_strSuffix));
	newDir.setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
	QFileInfoList filesInfos = newDir.entryInfoList();
	for (auto& fileInfo : filesInfos)
	{
		auto parts = fileInfo.baseName().split("_");
		if (parts.count() < 2)
		{
			logOut << QUaLog({
				QObject::tr("Ignoring history file %1. Incorrect file name format. Expected %2_xxxx.%3.")
					.arg(fileInfo.fileName()).arg(_strBaseName).arg(_strSuffix),
				QUaLogLevel::Warning,
				QUaLogCategory::History
			});
			continue;
		}
		QString strTimestamp = parts.last();
		bool ok = false;
		qint64 msSecSinceEpoc = strTimestamp.toLongLong(&ok);
		if (!ok)
		{
			logOut << QUaLog({
				QObject::tr("Ignoring history file %1. Incorrect file name format. Expected %2_xxxx.%3.")
					.arg(fileInfo.fileName()).arg(_strBaseName).arg(_strSuffix),
				QUaLogLevel::Warning,
				QUaLogCategory::History
			});
			continue;
		}
		QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(msSecSinceEpoc, QTimeZone::UTC);
		if (_dbFiles.contains(dateTime))
		{
			// remove from extsing
			oldFileSet.remove(dateTime);
			if (warnToLog)
			{
				logOut << QUaLog({
					QObject::tr("Ignoring history file %1. Repeated timestamp value %2.")
						.arg(fileInfo.fileName()).arg(msSecSinceEpoc),
					QUaLogLevel::Warning,
					QUaLogCategory::History
				});
			}
			continue;
		}
		QString strFilePath = fileInfo.absoluteFilePath();
		_dbFiles[dateTime].strFileName = strFilePath;
		logOut << QUaLog({
			QObject::tr("History file added %1.")
				.arg(strFilePath),
			QUaLogLevel::Info,
			QUaLogCategory::History
		});
	}
	// check if a file was removed
	auto oldFileList = oldFileSet.toList();
	while (!oldFileList.isEmpty())
	{
		QDateTime dateTime = oldFileList.takeFirst();
		logOut << QUaLog({
			QObject::tr("History file removed %1.")
				.arg(_dbFiles[dateTime].strFileName),
			QUaLogLevel::Info,
			QUaLogCategory::History
		});
		_dbFiles.remove(dateTime);
	}
	// return
	return true;
}

bool QUaMultiSqliteHistorizer::tableExists(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QString& tableName,
	bool& tableExists,
	QQueue<QUaLog>& logOut
)
{
	// get db file name
	const QString& strDbName = dbInfo.strFileName;
	// if not exists in cache, check database
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery query(db);
	QString strStmt = QString(
		"SELECT "
		"name "
		"FROM "
		"sqlite_master "
		"WHERE "
		"type='table' "
		"AND "
		"name=\"%1\";"
	).arg(tableName);
	if (!query.exec(strStmt))
	{
		logOut << QUaLog({
			QObject::tr("Error querying if table [%1] exists in %2 database. Sql : %3.")
				.arg(tableName)
				.arg(strDbName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// empty result if not exists
	tableExists = query.next();
	return true;
}

bool QUaMultiSqliteHistorizer::tableDataByNodeIdExists(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& nodeId,
	bool& tableExists,
	QQueue<QUaLog>& logOut)
{
	// save time by using cache instead of SQL
	if (dbInfo.dataPrepStmts.contains(nodeId))
	{
		tableExists = true;
		return true;
	}
	// use generic method
	bool ok = this->tableExists(
		dbInfo,
		db,
		nodeId,
		tableExists,
		logOut
	);
	// early exit if cannot continue
	if (!ok || !tableExists)
	{
		return ok;
	}
	// cache prepared statement if table exists
	ok = this->dataPrepareAllStmts(dbInfo, db, nodeId, logOut);
	return ok;
}

bool QUaMultiSqliteHistorizer::createDataNodeTable(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& nodeId,
	const QMetaType::Type& storeType,
	QQueue<QUaLog>& logOut)
{
	// get db file name
	const QString& strDbName = dbInfo.strFileName;
	// create query
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery query(db);
	QString strStmt = QString(
		"CREATE TABLE \"%1\""
		"("
		"[%1] INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
		"[Time] INTEGER NOT NULL,"
		"[Value] %2 NOT NULL,"
		"[Status] INTEGER NOT NULL"
		");"
	)
		.arg(nodeId)
		.arg(QUaMultiSqliteHistorizer::QtTypeToSqlType(storeType));
	if (!query.exec(strStmt))
	{
		logOut << QUaLog({
			QObject::tr("Could not create %1 table in %2 database. Sql : %3.")
				.arg(nodeId)
				.arg(strDbName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// create unique index on [Time] key for faster queries
	strStmt = QString("CREATE UNIQUE INDEX \"%1_Time\" ON \"%1\"(Time);").arg(nodeId);
	if (!query.exec(strStmt))
	{
		logOut << QUaLog({
			QObject::tr("Could not create %1_Time index on %1 table in %2 database. Sql : %3.")
				.arg(nodeId)
				.arg(strDbName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// cache prepared statement
	bool ok = this->dataPrepareAllStmts(dbInfo, db, nodeId, logOut);
	return ok;
}

bool QUaMultiSqliteHistorizer::insertDataPoint(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& nodeId,
	const QUaHistoryDataPoint& dataPoint,
	QQueue<QUaLog>& logOut)
{
	// get db file name
	const QString& strDbName = dbInfo.strFileName;
	Q_ASSERT(db.isValid() && db.isOpen());
	Q_ASSERT(dbInfo.dataPrepStmts.contains(nodeId));
	// check if multirow disabled
	if (_multiRowInsertSize <= 1)
	{
		QSqlQuery& query = dbInfo.dataPrepStmts[nodeId].writeHistoryData;
		query.bindValue(0, dataPoint.timestamp.toMSecsSinceEpoch());
		query.bindValue(1, dataPoint.value);
		query.bindValue(2, dataPoint.status);
		if (!query.exec())
		{
			logOut << QUaLog({
				QObject::tr("Could not insert new row in %1 table in %2 database. Sql : %3.")
					.arg(nodeId)
					.arg(strDbName)
					.arg(query.lastError().text()),
				QUaLogLevel::Error,
				QUaLogCategory::History
			});
			return false;
		}
		return true;
	}
	// insert first in block
	Q_ASSERT(dbInfo.dataPrepStmts.contains(nodeId));
	DataPointBlock & block = dbInfo.dataPrepStmts[nodeId].multiRowBlock;
	Q_ASSERT(!block.contains(dataPoint.timestamp));
	block[dataPoint.timestamp] = {
		dataPoint.value,
		dataPoint.status
	};
	Q_ASSERT(block.size() <= _multiRowInsertSize);
	// return if block not full
	if (block.size() < _multiRowInsertSize)
	{
		return true;
	}
	Q_ASSERT(block.size() == _multiRowInsertSize);
	// multirow insert if block full
	QSqlQuery& query = dbInfo.dataPrepStmts[nodeId].writeHistoryDataMultiRow;
	// insert block
	for (int i = 0; i < _multiRowInsertSize; i++)
	{
		auto currTime  = block.firstKey();
		auto currPoint = block.take(currTime);
		query.bindValue(3 * i    , currTime.toMSecsSinceEpoch());
		query.bindValue(3 * i + 1, currPoint.value);
		query.bindValue(3 * i + 2, currPoint.status);
	}
	if (!query.exec())
	{
		logOut << QUaLog({
			QObject::tr("Could not insert new row block in %1 table in %2 database. Sql : %3.")
				.arg(nodeId)
				.arg(strDbName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
		return false;
	}
	if (!block.isEmpty())
	{
		logOut << QUaLog({
			QObject::tr("Could not insert complete new row block in %1 table in %2 database. Remaining block size %3.")
				.arg(nodeId)
				.arg(strDbName)
				.arg(block.count()),
			QUaLogLevel::Warning,
			QUaLogCategory::History
		});
		return false;
	}
	return true;

}

bool QUaMultiSqliteHistorizer::dataPrepareAllStmts(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& nodeId,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery query(db);
	QString strStmt;
	// prepared statement for insert
	strStmt = QString(
		"INSERT INTO \"%1\" (Time, Value, Status) VALUES (:Time, :Value, :Status);"
	).arg(nodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	dbInfo.dataPrepStmts[nodeId].writeHistoryData = query;
	// prepared statement for multirow insert
	strStmt = QString(
		"INSERT INTO \"%1\" (Time, Value, Status) VALUES "
	).arg(nodeId);
	for (int i = 0; i < _multiRowInsertSize; i++)
	{
		if (i == _multiRowInsertSize - 1)
		{
			// last one
			strStmt += QString("(:t%1, :v%1, :s%1);").arg(i);
			break;
		}
		strStmt += QString("(:t%1, :v%1, :s%1), ").arg(i);
	}
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	dbInfo.dataPrepStmts[nodeId].writeHistoryDataMultiRow = query;
	// prepared statement for first timestamp
	strStmt = QString(
		"SELECT "
		"p.Time "
		"FROM "
		"\"%1\" p "
		"ORDER BY "
		"p.Time ASC "
		"LIMIT "
		"1;"
	).arg(nodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	query.setForwardOnly(true);
	dbInfo.dataPrepStmts[nodeId].firstTimestamp = query;
	// prepared statement for last timestamp
	strStmt = QString(
		"SELECT "
		"p.Time "
		"FROM "
		"\"%1\" p "
		"ORDER BY "
		"p.Time DESC "
		"LIMIT "
		"1;"
	).arg(nodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	query.setForwardOnly(true);
	dbInfo.dataPrepStmts[nodeId].lastTimestamp = query;
	// prepared statement for has timestamp
	strStmt = QString(
		"SELECT "
		"COUNT(*) "
		"FROM "
		"\"%1\" p "
		"WHERE "
		"p.Time = :Time;"
	).arg(nodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	query.setForwardOnly(true);
	dbInfo.dataPrepStmts[nodeId].hasTimestamp = query;
	// prepared statement for find timestamp from above
	strStmt = QString(
		"SELECT "
		"p.Time "
		"FROM "
		"\"%1\" p "
		"WHERE "
		"p.Time > :Time "
		"ORDER BY "
		"p.Time ASC "
		"LIMIT "
		"1;"
	).arg(nodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	query.setForwardOnly(true);
	dbInfo.dataPrepStmts[nodeId].findTimestampAbove = query;
	// prepared statement for find timestamp from below
	strStmt = QString(
		"SELECT "
		"p.Time "
		"FROM "
		"\"%1\" p "
		"WHERE "
		"p.Time < :Time "
		"ORDER BY "
		"p.Time DESC "
		"LIMIT "
		"1;"
	).arg(nodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	query.setForwardOnly(true);
	dbInfo.dataPrepStmts[nodeId].findTimestampBelow = query;
	// prepared statement for num points in range when end time is valid
	strStmt = QString(
		"SELECT "
		"COUNT(*) "
		"FROM "
		"\"%1\" p "
		"WHERE "
		"p.Time >= :TimeStart "
		"AND "
		"p.Time <= :TimeEnd "
		"ORDER BY "
		"p.Time ASC;"
	).arg(nodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	query.setForwardOnly(true);
	dbInfo.dataPrepStmts[nodeId].numDataPointsInRangeEndValid = query;
	// prepared statement for num points in range when end time is invalid
	strStmt = QString(
		"SELECT "
		"COUNT(*) "
		"FROM "
		"\"%1\" p "
		"WHERE "
		"p.Time >= :TimeStart "
		"ORDER BY "
		"p.Time ASC;"
	).arg(nodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	query.setForwardOnly(true);
	dbInfo.dataPrepStmts[nodeId].numDataPointsInRangeEndInvalid = query;
	// prepared statement for reading data points
	strStmt = QString(
		"SELECT "
		"p.Time, p.Value, p.Status "
		"FROM "
		"\"%1\" p "
		"WHERE "
		"p.Time >= :Time "
		"ORDER BY "
		"p.Time ASC "
		"LIMIT "
		":Limit "
		"OFFSET "
		":Offset;"
	).arg(nodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	query.setForwardOnly(true);
	dbInfo.dataPrepStmts[nodeId].readHistoryData = query;
	// success
	return true;
}

bool QUaMultiSqliteHistorizer::prepareStmt(
	const DatabaseInfo& dbInfo,
	QSqlQuery& query, 
	const QString& strStmt,
	QQueue<QUaLog>& logOut)
{
	if (!query.prepare(strStmt))
	{
		logOut << QUaLog({
			QObject::tr("Error preparing statement %1 for %2 database. Sql : %3.")
				.arg(strStmt)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
		return false;
	}
	return true;
}

bool QUaMultiSqliteHistorizer::handleTransactions(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db, 
	QQueue<QUaLog>& logOut)
{
	// return success if transactions disabled
	if (_timeoutTransaction == 0)
	{
		return true;
	}
	// return success if transaction currently opened
	if (_timerTransaction.isActive())
	{
		return true;
	}
	// open new transaction if required
	if (!db.transaction())
	{
		dbInfo.openedTransaction = false;
		logOut << QUaLog({
			QObject::tr("Failed to begin transaction in %1 database. Sql : %2.")
				.arg(dbInfo.strFileName)
				.arg(db.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
		});
		return false;
	}
	dbInfo.openedTransaction = true;
	// start timer to stop transaction after specified period (see constructor)
	_timerTransaction.start(_timeoutTransaction);
	return true;
}

QMetaType::Type QUaMultiSqliteHistorizer::QVariantToQtType(const QVariant& value)
{
	return static_cast<QMetaType::Type>(
		value.type() < 1024 ?
		value.type() :
		value.userType()
	);
}

const QString QUaMultiSqliteHistorizer::QtTypeToSqlType(const QMetaType::Type& qtType)
{

	if (!QUaMultiSqliteHistorizer::_hashTypes.contains(qtType))
	{
		qWarning() << "[UNKNOWN TYPE]" << QMetaType::typeName(qtType);
		Q_ASSERT_X(false, "QUaMultiSqliteHistorizer::QtTypeToSqlType", "Unknown type.");
	}
	return QUaMultiSqliteHistorizer::_hashTypes.value(qtType, "BLOB");
}

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS

bool QUaMultiSqliteHistorizer::tableEventTypeByNodeIdExists(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db, 
	const QUaNodeId& eventTypeNodeId, 
	const QUaHistoryEventPoint& eventPoint,
	bool& tableExists, 
	QQueue<QUaLog>& logOut)
{
	// save time by using cache instead of SQL
	if (dbInfo.eventTypePrepStmts.contains(eventTypeNodeId))
	{
		tableExists = true;
		return true;
	}
	// use generic method
	bool ok = this->tableExists(
		dbInfo,
		db,
		eventTypeNodeId,
		tableExists,
		logOut
	);
	// early exit if cannot continue
	if (!ok || !tableExists)
	{
		return ok;
	}
	// cache prepared statement if table exists
	// NOTE : only if there are fields, because in some places this is called with
	//        eventPoint == QUaHistoryEventPoint()
	if (!eventPoint.fields.isEmpty())
	{
		ok = this->eventTypePrepareStmt(dbInfo, db, eventTypeNodeId, eventPoint, logOut);
	}
	return ok;
}

bool QUaMultiSqliteHistorizer::createEventTypeTable(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& eventTypeNodeId,
	const QUaHistoryEventPoint& eventPoint,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery query(db);
	QString strStmt = QString(
		"CREATE TABLE \"%1\""
		"("
		"[%1] INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, "
	).arg(eventTypeNodeId);
	// add columns
	auto listColumns = eventPoint.fields.keys();
	std::sort(listColumns.begin(), listColumns.end());
	auto i = listColumns.begin();
	while (i != listColumns.end())
	{
		auto& name = *i;
		auto& value = eventPoint.fields[name];
		strStmt += QString("[%1] %2")
			.arg(QUaQualifiedName::reduceName(name, "_"))
			.arg(
				QUaMultiSqliteHistorizer::QtTypeToSqlType(QUaMultiSqliteHistorizer::QVariantToQtType(value)));
		if (++i != listColumns.end())
		{
			strStmt += ", ";
		}
	}
	// close statement
	strStmt += ");";
	// execute
	if (!query.exec(strStmt))
	{
		logOut << QUaLog({
			QObject::tr("Could not create %1 event type table in %2 database. Sql : %3.")
				.arg(eventTypeNodeId)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// cache prepared statement if table exists
	bool ok = this->eventTypePrepareStmt(dbInfo, db, eventTypeNodeId, eventPoint, logOut);
	return ok;
}

bool QUaMultiSqliteHistorizer::eventTypePrepareStmt(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& eventTypeNodeId,
	const QUaHistoryEventPoint& eventPoint,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery query(db);
	QString strStmt;
	// prepared statement for insert
	strStmt = QString(
		"INSERT INTO \"%1\" ("
	).arg(eventTypeNodeId);
	// iterate column names (e.g. Time, Value, Status)
	QHashIterator<QUaBrowsePath, QVariant> i(eventPoint.fields);
	while (i.hasNext()) {
		i.next();
		auto& name = i.key();
		strStmt += QString("%1")
			.arg(QUaQualifiedName::reduceName(name, "_"));
		if (i.hasNext())
		{
			strStmt += ", ";
		}
	}
	// continue statement
	strStmt += ") VALUES (";
	// iterate column placeholders (e.g. :Time, :Value, :Status)
	i.toFront();
	while (i.hasNext()) {
		i.next();
		auto& name = i.key();
		strStmt += QString(":%1")
			.arg(QUaQualifiedName::reduceName(name, "_"));
		if (i.hasNext())
		{
			strStmt += ", ";
		}
	}
	// close statement
	strStmt += ");";
	// prepare
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	dbInfo.eventTypePrepStmts[eventTypeNodeId] = query;
	return true;
}

QString QUaMultiSqliteHistorizer::eventTypesTable = "EventTypeTableNames";

bool QUaMultiSqliteHistorizer::tableEventTypeNameExists(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	bool& tableExists,
	QQueue<QUaLog>& logOut)
{
	// save time by using cache instead of SQL
	if (dbInfo.eventTypeNamePrepStmt.contains(QUaMultiSqliteHistorizer::eventTypesTable))
	{
		tableExists = true;
		return true;
	}
	// use generic method
	bool ok = this->tableExists(
		dbInfo,
		db,
		QUaMultiSqliteHistorizer::eventTypesTable,
		tableExists,
		logOut
	);
	// early exit if cannot continue
	if (!ok || !tableExists)
	{
		return ok;
	}
	// cache prepared statement if table exists
	ok = this->eventTypeNamePrepareStmt(dbInfo, db, logOut);
	return ok;
}

bool QUaMultiSqliteHistorizer::createEventTypeNameTable(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery query(db);
	QString strStmt = QString(
		"CREATE TABLE \"%1\""
		"("
		"[%1] INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
		"[TableName] TEXT NOT NULL"
		");"
	).arg(QUaMultiSqliteHistorizer::eventTypesTable);
	if (!query.exec(strStmt))
	{
		logOut << QUaLog({
			QObject::tr("Could not create %1 table in %2 database. Sql : %3.")
				.arg(QUaMultiSqliteHistorizer::eventTypesTable)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// create unique index on [TableName] key for faster queries
	strStmt = QString("CREATE UNIQUE INDEX \"%1_TableName\" ON \"%1\"(TableName);")
		.arg(QUaMultiSqliteHistorizer::eventTypesTable);
	if (!query.exec(strStmt))
	{
		logOut << QUaLog({
			QObject::tr("Could not create %1_TableName index on %1 table in %2 database. Sql : %3.")
				.arg(QUaMultiSqliteHistorizer::eventTypesTable)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// cache prepared statement
	bool ok = this->eventTypeNamePrepareStmt(dbInfo, db, logOut);
	return ok;
}

bool QUaMultiSqliteHistorizer::eventTypeNamePrepareStmt(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery query(db);
	QString strStmt;
	// prepared statement for insert
	strStmt = QString(
		"INSERT INTO \"%1\" (TableName) VALUES (:TableName);"
	).arg(QUaMultiSqliteHistorizer::eventTypesTable);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	dbInfo.eventTypeNamePrepStmt[QUaMultiSqliteHistorizer::eventTypesTable].insertEventTypeName = query;
	// prepared statement select exsiting
	strStmt = QString(
		"SELECT "
		"t.%1 "
		"FROM "
		"%1 t "
		"WHERE "
		"t.TableName = :TableName"
	).arg(QUaMultiSqliteHistorizer::eventTypesTable);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	dbInfo.eventTypeNamePrepStmt[QUaMultiSqliteHistorizer::eventTypesTable].selectEventTypeName = query;
	return true;
}

bool QUaMultiSqliteHistorizer::tableEmitterByNodeIdExists(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db, 
	const QUaNodeId& emitterNodeId,
	bool& tableExists,
	QQueue<QUaLog>& logOut)
{
	// save time by using cache instead of SQL
	if (dbInfo.emitterPrepStmts.contains(emitterNodeId))
	{
		tableExists = true;
		return true;
	}
	// use generic method
	bool ok = this->tableExists(
		dbInfo,
		db,
		emitterNodeId,
		tableExists,
		logOut
	);
	// early exit if cannot continue
	if (!ok || !tableExists)
	{
		return ok;
	}
	// cache prepared statement if table exists
	ok = this->emitterPrepareStmt(dbInfo, db, emitterNodeId, logOut);
	return ok;
}

bool QUaMultiSqliteHistorizer::createEmitterTable(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& emitterNodeId,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery query(db);
	QString strStmt = QString(
		"CREATE TABLE \"%1\""
		"("
		"[%1] INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
		"[Time] INTEGER NOT NULL, "
		"[EventType] INTEGER NOT NULL, "
		"[EventId] INTEGER NOT NULL"
		");"
	).arg(emitterNodeId);
	if (!query.exec(strStmt))
	{
		logOut << QUaLog({
			QObject::tr("Could not create %1 table in %2 database. Sql : %3.")
				.arg(emitterNodeId)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// create index on [Time, EventType] key for faster queries
	strStmt = QString("CREATE INDEX \"%1_Time_EventType\" ON \"%1\"(Time, EventType);")
		.arg(emitterNodeId);
	if (!query.exec(strStmt))
	{
		logOut << QUaLog({
			QObject::tr("Could not create %1_Time_EventType on %1 table in %2 database. Sql : %3.")
				.arg(emitterNodeId)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// cache prepared statement
	bool ok = this->emitterPrepareStmt(dbInfo, db, emitterNodeId, logOut);
	return ok;
}

bool QUaMultiSqliteHistorizer::emitterPrepareStmt(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& emitterNodeId,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	QSqlQuery query(db);
	QString strStmt;
	// prepared statement for insert
	strStmt = QString(
		"INSERT INTO \"%1\" (Time, EventType, EventId) VALUES (:Time, :EventType, :EventId);"
	).arg(emitterNodeId);
	if (!this->prepareStmt(dbInfo, query, strStmt, logOut))
	{
		return false;
	}
	dbInfo.emitterPrepStmts[emitterNodeId] = query;
	return true;
}

bool QUaMultiSqliteHistorizer::insertEventPoint(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& eventTypeNodeId,
	const QUaHistoryEventPoint& eventPoint,
	qint64& outEventKey,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	Q_ASSERT(dbInfo.eventTypePrepStmts.contains(eventTypeNodeId));
	QSqlQuery& query = dbInfo.eventTypePrepStmts[eventTypeNodeId];
	// iterate column placeholders (e.g. Time, Value, Status)
	// and bind values
	QHashIterator<QUaBrowsePath, QVariant> i(eventPoint.fields);
	while (i.hasNext()) {
		i.next();
		auto& name = i.key();
		auto& value = i.value();
		if (!value.isValid())
		{
			continue;
		}
		auto type = QUaMultiSqliteHistorizer::QVariantToQtType(value);
		query.bindValue(
			QString(":%1").arg(QUaQualifiedName::reduceName(name, "_")),
			type == QMetaType::UChar ?
			value.toUInt() :
			type == QMetaType::QDateTime ?
			value.toDateTime().toMSecsSinceEpoch() :
			value
		);
	}
	if (!query.exec())
	{
		logOut << QUaLog({
			QObject::tr("Could not insert new row in %1 table in %2 database. Sql : %3.")
				.arg(eventTypeNodeId)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// get new key
	outEventKey = static_cast<qint64>(query.lastInsertId().toULongLong());
	return true;
}

bool QUaMultiSqliteHistorizer::selectOrInsertEventTypeName(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& eventTypeNodeId,
	qint64& outEventTypeKey,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	Q_ASSERT(dbInfo.eventTypeNamePrepStmt.contains(QUaMultiSqliteHistorizer::eventTypesTable));
	// first try to find with select
	QSqlQuery& querySelect = dbInfo.eventTypeNamePrepStmt[QUaMultiSqliteHistorizer::eventTypesTable].selectEventTypeName;
	//  bind values
	querySelect.bindValue(0, eventTypeNodeId.toXmlString());
	// execute
	if (!querySelect.exec())
	{
		logOut << QUaLog({
			QObject::tr("Could not select row in %1 table in %2 database. Sql : %3.")
				.arg(QUaMultiSqliteHistorizer::eventTypesTable)
				.arg(dbInfo.strFileName)
				.arg(querySelect.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// check if exists
	if (querySelect.next())
	{
		outEventTypeKey = static_cast<qint64>(querySelect.value(0).toLongLong());
		return true;
	}
	// insert
	QSqlQuery& queryInsert = dbInfo.eventTypeNamePrepStmt[QUaMultiSqliteHistorizer::eventTypesTable].insertEventTypeName;
	//  bind values
	queryInsert.bindValue(0, eventTypeNodeId.toXmlString());
	// execute
	if (!queryInsert.exec())
	{
		logOut << QUaLog({
			QObject::tr("Could not insert new row in %1 table in %2 database. Sql : %3.")
				.arg(QUaMultiSqliteHistorizer::eventTypesTable)
				.arg(dbInfo.strFileName)
				.arg(queryInsert.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	// get new key
	outEventTypeKey = static_cast<qint64>(queryInsert.lastInsertId().toULongLong());
	return true;
}

bool QUaMultiSqliteHistorizer::insertEventReferenceInEmitterTable(
	DatabaseInfo& dbInfo,
	QSqlDatabase& db,
	const QUaNodeId& emitterNodeId,
	const QDateTime& timestamp,
	qint64& outEventTypeKey,
	qint64& outEventKey,
	QQueue<QUaLog>& logOut)
{
	Q_ASSERT(db.isValid() && db.isOpen());
	Q_ASSERT(dbInfo.emitterPrepStmts.contains(emitterNodeId));
	QSqlQuery& query = dbInfo.emitterPrepStmts[emitterNodeId];
	//  bind values
	query.bindValue(":Time"     , timestamp.toMSecsSinceEpoch());
	query.bindValue(":EventType", outEventTypeKey);
	query.bindValue(":EventId"  , outEventKey);
	// execute
	if (!query.exec())
	{
		logOut << QUaLog({
			QObject::tr("Could not insert new row in %1 table in %2 database. Sql : %3.")
				.arg(emitterNodeId)
				.arg(dbInfo.strFileName)
				.arg(query.lastError().text()),
			QUaLogLevel::Error,
			QUaLogCategory::History
			});
		return false;
	}
	return true;
}


#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

#endif // UA_ENABLE_HISTORIZING
