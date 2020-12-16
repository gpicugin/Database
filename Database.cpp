#include "Database.h"
#include "Timer.h"


#define L_HEAD_FMT_IN  ",%10s,%10s,%10s"
#define L_HEAD_FMT_IN2 L_HEAD_FMT_IN L_HEAD_FMT_IN
#define L_HEAD_FMT_IN4 L_HEAD_FMT_IN2 L_HEAD_FMT_IN2
#define L_HEAD_FMT_OUT ",%10s,%10s"
#define L_HEAD_FMT_OUT2 L_HEAD_FMT_OUT L_HEAD_FMT_OUT

#define L_DATA_FMT_IN  ",%10u,%10.6f,%10u"
#define L_DATA_FMT_IN2 L_DATA_FMT_IN L_DATA_FMT_IN
#define L_DATA_FMT_IN4 L_DATA_FMT_IN2 L_DATA_FMT_IN2
#define L_DATA_FMT_OUT ",%10u,%10.6f"
#define L_DATA_FMT_OUT2 L_DATA_FMT_OUT L_DATA_FMT_OUT 



#ifdef HIER_MODE_SUPPORTED
#define L_FMT_HEAD   "%20s,%15s" L_HEAD_FMT_IN4 L_HEAD_FMT_OUT2 "\n"
#define L_FMT_DATA   "%20s,%15d" L_DATA_FMT_IN4 L_DATA_FMT_OUT2 "\n"
#else
#define L_FMT_HEAD   "%20s,%15s" L_HEAD_FMT_IN2 L_HEAD_FMT_OUT "\n"
#define L_FMT_DATA   "%20s,%15d" L_DATA_FMT_IN2 L_DATA_FMT_OUT "\n"
#endif



static uint32_t sqliteReset(sqlite3_stmt* pStmt)
{
    uint32_t errorCode = sqlite3_reset(pStmt);
    if (errorCode != SQLITE_OK)
    {
#ifdef DEBUG
        std::cout << "WARNING!\n";
#endif		
    }
    return errorCode;
}

struct SQLiteRequest
{
    sqlite3_stmt* pStmt;

    SQLiteRequest(sqlite3* db, const std::string& req)
    {
        int errCode = sqlite3_prepare_v2(db, req.c_str(), -1, &pStmt, 0);
    }
    ~SQLiteRequest()
    {
        sqliteReset(pStmt);//сбросить команду
        sqlite3_finalize(pStmt);//очистить, завершить работу команды
    }
};


static bool isDataSourceSupported(DataSource source)
{
    switch (source)
    {
    default: return false;
    case DS_IN_HP1: return true;
    case DS_IN_HP2: return true;
    case DS_OUT_HP: return true;
#ifdef HIER_MODE_SUPPORTED
    case DS_IN_LP1: return true;
    case DS_IN_LP2: return true;
    case DS_OUT_LP: return true;
#endif
    }
}

static std::string getTableName(DataSource source)
{
    switch (source)
    {
    default: return "";
    case DS_IN_HP1: return  "'HP1'";
    case DS_IN_HP2: return  "'HP2'";
    case DS_OUT_HP: return "'HPOut'";

    case DS_IN_LP1: return "'LP1'";
    case DS_IN_LP2: return "'LP2'";
    case DS_OUT_LP: return "'LPOut'";
    }
}

Database::Database(const std::string& fileName, bool bRecreate) : m_pDb(NULL), m_limit(5000),
m_atomicDumpSize(1000), m_transPackSize(100), m_dbFileName(fileName)
{
    createEmptyDb();
    open(fileName, bRecreate);
    createTables();
}

Database::~Database()
{
    close();
}

bool Database::open(const std::string& fileName, bool bRecreate)
{
    m_DbMutex.lock();
    uint32_t iResult = sqlite3_open_v2(fileName.c_str(), &m_pDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
        | SQLITE_OPEN_FULLMUTEX, NULL);
    if (bRecreate && iResult == SQLITE_OK)
    {
        std::stringstream ss;
        for (uint32_t i = 0; i < DS_COUNT; i++)
        {
            DataSource DS = static_cast<DataSource>(i);
            ss << "DROP TABLE IF EXISTS " << getTableName(DS) << ";";

            SQLiteRequest req(m_pDb, ss.str());
            sqlite3_step(req.pStmt); //выполнить команду

            ss.str("");
        }
    }
    m_DbMutex.unlock();
    return(iResult == SQLITE_OK);
}

void Database::close()
{
    m_DbMutex.lock();
    sqlite3_close(m_pDb);
    m_pDb = NULL;
    m_DbMutex.unlock();
}

void Database::createTables()
{
    std::stringstream ss;
    sqlite3_exec(m_pDb, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (uint32_t i = 0; i < DS_IN_TOTAL; i++)
    {
        DataSource DS = static_cast<DataSource>(i);
        if (!isDataSourceSupported(DS))
            continue;
        ss << "CREATE TABLE IF NOT EXISTS " << getTableName(DS) << "(\
							delayFactor				float,\
							mediaLossRate			integer,\
							rate					integer,\
							pcrArray				blob,\
							samples			integer,\
							time					integer);";

        SQLiteRequest req(m_pDb, ss.str());
        sqlite3_step(req.pStmt); //выполнить команду		
        ss.str("");
    }
    for (uint32_t i = DS_OUT_BASE; i < DS_COUNT; i++)
    {
        DataSource DS = static_cast<DataSource>(i);
        if (!isDataSourceSupported(DS))
            continue;
        ss << "CREATE TABLE IF NOT EXISTS " << getTableName(DS) << "(\
							delayFactor				float,\
							rate					integer,\
							pcrArray				blob,\
							samples			integer,\
							time					integer);";

        SQLiteRequest req(m_pDb, ss.str());
        sqlite3_step(req.pStmt); //выполнить команду			
        ss.str("");
    }
    sqlite3_exec(m_pDb, "END TRANSACTION", NULL, NULL, NULL);
}

time_t Database::getStartTime()
{
    m_DbMutex.lock();
    SQLiteRequest req(m_pDb, "SELECT MIN(time) as time FROM 'HP1'");

    time_t startTime = 0;
    if (sqlite3_step(req.pStmt) == SQLITE_ROW)
    {
        startTime = sqlite3_column_int(req.pStmt, 0);
    }
    m_DbMutex.unlock();
    return startTime;
}

uint32_t Database::internalGetTotalSamples()
{
    SQLiteRequest req(m_pDb, "SELECT COUNT(*) FROM 'HP1'");
    uint32_t totalSamples;
    sqlite3_step(req.pStmt);
    totalSamples = sqlite3_column_int(req.pStmt, 0);
    return totalSamples;
}

uint32_t Database::getTotalSamples()
{
    m_DbMutex.lock();
    uint32_t result = internalGetTotalSamples();
    m_DbMutex.unlock();
    return result;
}

bool Database::verifyIntegrity()
{
    uint32_t iTotal = 0;
    uint32_t iCurrent = 0;
    bool iResult = true;
    std::stringstream ss;
    m_DbMutex.lock();
    for (uint32_t i = 0; i < DS_COUNT; i++)
    {
        DataSource DS = static_cast<DataSource>(i);
        if (!isDataSourceSupported(DS))
            continue;
        ss << "SELECT COUNT(*) FROM " << getTableName(DS);
        SQLiteRequest req(m_pDb, ss.str());

        sqlite3_step(req.pStmt);
        iCurrent = sqlite3_column_int(req.pStmt, 0);
        if (iTotal == 0 && iCurrent != 0)
        {
            iTotal = iCurrent;
        }
        else if (iTotal != iCurrent)
        {
            iResult = false;
            break;
        }
        ss.str("");
    }
    m_DbMutex.unlock();
    return iResult;
}

void Database::add(time_t startTime, const LogSample* samples, uint32_t count)
{
    m_DbMutex.lock();
    static uint32_t counter = 0;
    for (uint32_t i = 0; i < count; i++)
    {       
        if (i % 100 == 0 && i != 0)
            std::cout << i << " -  " << counter << " ITERATION" << '\n';
        if (internalGetTotalSamples() >= m_limit)
        {
            deleteFirstSample();
        }
        addToInput(startTime + i, samples[i]);
        addToOutput(startTime + i, samples[i]);
    }
    counter++;
    m_DbMutex.unlock();
}


void Database::addT(time_t startTime, const LogSample* samples, uint32_t count)
{
    m_DbMutex.lock();
    uint32_t entire = count / m_transPackSize; // кол-во целых пачек
    uint32_t balance = count % m_transPackSize; // последняя пачка    

    uint32_t transPackSize;
    uint32_t progress;
    uint32_t j;
    for (uint32_t i = 0; i <= entire; i++)
    {
        j = i * m_transPackSize;
        if (i != entire)
            transPackSize = m_transPackSize;
        else
            transPackSize = balance;

        progress = (balance == 0) ? (j) : (i != entire) ? (j) : ((i - 1) * m_transPackSize + balance);
        if (progress % 10000 == 0 && progress != 0)
            std::cout << progress << '\n';
        uint32_t total = internalGetTotalSamples();
        if (total >= m_limit)
            deleteFirstNSamples(total - m_limit + transPackSize);
        addToInputT(startTime + j, samples + j, transPackSize);
        addToOutputT(startTime + j, samples + j, transPackSize);
    }
    m_DbMutex.unlock();
}




uint32_t Database::get(LogSample* samples, uint32_t count, time_t startTime)
{
    uint32_t localCount = 0;
    uint32_t iResult = 0;
    while (count > 0)
    {
        localCount = internalGet(samples + iResult, count, startTime);
        if (localCount == 0)
            break;
        count -= localCount;
        iResult += localCount;
    }
    return iResult;
}

void Database::clear()
{
    std::stringstream ss;
    m_DbMutex.lock();  
    sqlite3_exec(m_pDb, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (uint32_t i = 0; i < DS_COUNT; i++)
    {
        DataSource DS = static_cast<DataSource>(i);
        ss << "DROP TABLE IF EXISTS " << getTableName(DS) << ";";

        SQLiteRequest req(m_pDb, ss.str());
        sqlite3_step(req.pStmt); //выполнить команду

        ss.str("");
    }
    sqlite3_exec(m_pDb, "END TRANSACTION", NULL, NULL, NULL);
    createTables();
    m_DbMutex.unlock();
}

int delFucnt(std::string buffName)
{
    int result = remove(buffName.c_str());
    return result;
}

void Database::clearFake(std::ofstream& fs)
{
    Timer timer;
    
    timer.start();
    close();
    double timeStampClose = timer.stop();
    m_DbMutex.lock();

    // rename    
    std::string buffName = m_dbFileName;
    buffName.erase(buffName.size() - 3, 3);
    buffName += "Del.db";
    rename(m_dbFileName.c_str(), buffName.c_str());
    std::thread deleteThread(delFucnt, buffName);
    deleteThread.detach();

   

    timer.start();
#ifdef OS_WINDOWS
    std::string bashCommand;
    bashCommand = "cp " + m_dbEmptyFileName + " " + m_dbFileName;
    system(bashCommand.c_str());
#elif defined OS_LINUX
    system("copy c:\\repos-projects\\sqlitepich\\database\\database\\mydatabaselinuxempty.db \
				   c:\\repos-projects\\sqlitepich\\database\\database\\mydatabaselinuxempty.db ");
#endif
    double timeStampCP = timer.stop();

    // подмена интерфейса
    m_DbMutex.unlock();
    timer.start();
    this->open(m_dbFileName, false);
    double timeStampOpen = timer.stop();
    fs << timeStampClose << ", " << timeStampCP << ", " << timeStampOpen << ", ";
}

bool Database::dump(const std::string& fileName)
{
    std::ofstream fs;
    std::string logName = "dumpLog.csv";
    fs.open(logName);
    fs << "get\n"; // << "fwrite, " << "summ, " << "result, \n";
    double result = 0;
    Timer timer;
    FILE* f = fopen(fileName.c_str(), "wt");

    char buffer[512];
    uint32_t numBytes;

    // create and write header
    numBytes = createLogHeaderCSV(buffer, sizeof(buffer));
    if (numBytes == 0)
        return false;

    fwrite(buffer, numBytes, 1, f);

    LogSample* samples = new LogSample[m_atomicDumpSize];
    int cnt = 0;
    bool flag = true;
    time_t nextTimeStamp = getStartTime();// +100 * m_atomicDumpSize;
    int j = 0;
    do
    {
        double summ = 0;
        if (j != 0)
            std::cout << j * m_atomicDumpSize << "\n";
        timer.start();
        cnt = internalGet(samples, m_atomicDumpSize, nextTimeStamp);
        double getTime = timer.stop();
        fs << getTime << "\n";
        for (uint32_t i = 0; i < cnt; i++)
        {
            numBytes = createLogEntryCSV(buffer, sizeof(buffer), (nextTimeStamp - cnt) + i, samples[i]);
            if (numBytes == 0)
                return false;
            fwrite(buffer, numBytes, 1, f);
        }  
        j++;
    } while (cnt > 0);
    delete[] samples;
    fs.close();
    fclose(f);
    return true;
}


bool Database::deleteFirstSample()
{
    std::stringstream ss;
    for (uint32_t i = 0; i < DS_COUNT; i++)
    {
        DataSource DS = static_cast<DataSource>(i);
        if (!isDataSourceSupported(DS))
            continue;
        ss << "DELETE FROM " << getTableName(DS) << " WHERE time IN (SELECT time FROM " << getTableName(DS)
            << " WHERE TIME > 0 LIMIT 1)";
        SQLiteRequest req(m_pDb, ss.str());

        sqlite3_step(req.pStmt); //выполнить команду
        ss.str("");
    }
    return true;
}

bool Database::deleteFirstNSamples(uint32_t n)
{
    std::stringstream ss;

    sqlite3_exec(m_pDb, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (uint32_t i = 0; i < DS_COUNT; i++)
    {
        DataSource DS = static_cast<DataSource>(i);

        if (!isDataSourceSupported(DS))
            continue;
        ss << "DELETE FROM " << getTableName(DS) << " WHERE time IN (SELECT time FROM " << getTableName(DS)
            << " WHERE TIME > 0 LIMIT " << n << ")\n";
        SQLiteRequest req(m_pDb, ss.str());
        int errCode = sqlite3_step(req.pStmt); //выполнить команду
        ss.str("");
    }
    sqlite3_exec(m_pDb, "COMMIT TRANSACTION", NULL, NULL, NULL);
    return true;
}

uint32_t Database::internalGet(LogSample* samples, uint32_t count, time_t& startTime)
{
    Timer timer;
   /* std::string filename = "iGetLog.csv";
    std::ofstream fs;*/
    //fs.open(filename, std::ios::app);
    if (count > m_atomicDumpSize)
        count = m_atomicDumpSize;

    std::stringstream ss;
    uint32_t verifyArr[DS_COUNT] = { 0 };

    m_DbMutex.lock();
    for (uint32_t i = 0; i < DS_COUNT; i++)
    {
        DataSource DS = static_cast<DataSource>(i);
        if (!isDataSourceSupported(DS))
            continue;

        ss << "SELECT * FROM " << getTableName(DS) << " WHERE time >= " << startTime << " LIMIT " << count;

        SQLiteRequest req(m_pDb, ss.str());
        if (i < DS_IN_TOTAL)
        {
            if (i == DS_IN_HP1)
            {
                DBData data = getInputData(DS, samples, count, req.pStmt);
                startTime = data.startTime;
                verifyArr[i] = data.counter;
            }
            else
                verifyArr[i] = getInputData(DS, samples, count, req.pStmt).counter;
        }
        else
        {
            verifyArr[i] = getOutputData(DS, samples, count, req.pStmt).counter;
        }
        ss.str("");
    }
    m_DbMutex.unlock();

    uint32_t iResult = verifyArr[0];

    for (int i = 1; i < DS_COUNT; i++)
    {
        DataSource DS = static_cast<DataSource>(i);
        if (!isDataSourceSupported(DS))
            continue;

        if (iResult != verifyArr[i])
            return 0;
    }

    startTime += iResult;
    //fs.close();
    return iResult;
}

void Database::addToInput(time_t currTime, const LogSample& sample)
{
    Timer timer;
    const InputData* arrayIn[DS_IN_TOTAL] = { &sample.hp1,  &sample.lp1,
                                                                 &sample.hp2,  &sample.lp2 };
    std::stringstream ss;
    for (uint32_t i = 0; i < DS_IN_TOTAL; i++)
    {
        DataSource DS = static_cast<DataSource>(i + DS_IN_BASE);

        if (!isDataSourceSupported(DS))
            continue;

        ss << "INSERT INTO " << getTableName(DS) << "(delayFactor, mediaLossRate, rate, pcrArray,\
										samples, time)  VALUES(?,?,?,?,?,?);";
        const InputData* in = arrayIn[i];
        timer.start(); // 1
        SQLiteRequest req(m_pDb, ss.str());
        double timeStampReq = timer.stop();
        timer.start(); // 2
        sqlite3_bind_double(req.pStmt, 1, in->delayFactor);
        double timeStampDelayFactor = timer.stop();
        //std::cout << in->delayFactor << "\n";
        timer.start(); // 3
        sqlite3_bind_int(req.pStmt, 2, in->mediaLossRate);
        double timeStampMediaLossRate = timer.stop();
        //std::cout << in->mediaLossRate << "\n";
        timer.start(); // 4
        sqlite3_bind_int(req.pStmt, 3, in->rate);
        double timeStampRate = timer.stop();
        //std::cout << in->rate << "\n";
        timer.start(); // 5
        sqlite3_bind_blob(req.pStmt, 4, in->pcrArray, sizeof(float) * in->samples, NULL);
        double timeStampArr = timer.stop();
        //std::cout << sizeof(float) * in->samples << "\n";
        timer.start(); // 6
        sqlite3_bind_int(req.pStmt, 5, in->samples);
        double timeStampsamples = timer.stop();
        //std::cout << in->samples << "\n";
        timer.start(); // 7
        sqlite3_bind_int(req.pStmt, 6, currTime);
        double timeStampCurrTime = timer.stop();
        //std::cout << currTime << "\n";
        timer.start(); // 8
        sqlite3_step(req.pStmt);
        double timeStampStep = timer.stop();        
        ss.str("");
    }
}

void Database::addToOutput(time_t currTime, const LogSample& sample)
{
    Timer timer;
    const OutputData* arrayOut[DS_OUT_TOTAL] = { &sample.hpOut, &sample.lpOut };

    std::stringstream ss;
    for (uint32_t i = 0; i < DS_OUT_TOTAL; i++)
    {
        DataSource DS = static_cast<DataSource>(i + DS_OUT_BASE);

        if (!isDataSourceSupported(DS))
            continue;

        const OutputData* out = arrayOut[i];
        ss << "INSERT INTO " << getTableName(DS) << "(delayFactor, rate, pcrArray,\
										samples, time)  VALUES(?,?,?,?,?);";

        timer.start(); // 1
        SQLiteRequest req(m_pDb, ss.str());
        double timeStampReq = timer.stop();
        timer.start(); // 2
        sqlite3_bind_double(req.pStmt, 1, out->delayFactor);
        double timeStampDelayFactor = timer.stop();
        timer.start(); // 3
        sqlite3_bind_int(req.pStmt, 2, out->rate);
        double timeStampRate = timer.stop();
        timer.start(); // 4
        sqlite3_bind_blob(req.pStmt, 3, out->pcrArray, sizeof(float) * out->samples, NULL);
        double timeStampArr = timer.stop();
        timer.start(); // 5
        sqlite3_bind_int(req.pStmt, 4, out->samples);
        double timeStampsamples = timer.stop();
        timer.start(); // 6
        sqlite3_bind_int(req.pStmt, 5, currTime);
        double timeStampCurrTime = timer.stop();
        timer.start(); // 7
        sqlite3_step(req.pStmt);
        double timeStampStep = timer.stop(); 
        ss.str("");
    }
}

void Database::addToInputT(time_t currTime, const LogSample* samples, uint32_t count)
{
    if (count == 0)
        return;
    std::stringstream ss;
    Timer timer;
    for (uint32_t i = 0; i < DS_IN_TOTAL; i++)
    {
        DataSource DS = static_cast<DataSource>(i + DS_IN_BASE);

        if (!isDataSourceSupported(DS))
            continue;
        //timer.start();
        sqlite3_exec(m_pDb, "BEGIN TRANSACTION", NULL, NULL, NULL);
        //double timeStampBegin = timer.stop();
        //timer.start();
        for (uint32_t j = 0; j < count; j++)
        {
            ss << "INSERT INTO " << getTableName(DS) << "(delayFactor, mediaLossRate, rate, pcrArray,\
										samples, time)  VALUES(?,?,?,?,?,?);\n";
            LogSample sample = samples[j];
            const InputData* in = static_cast<InputData*>(getSample(sample, DS));
            SQLiteRequest req(m_pDb, ss.str());
            sqlite3_bind_double(req.pStmt, 1, in->delayFactor);
            sqlite3_bind_int(req.pStmt, 2, in->mediaLossRate);
            sqlite3_bind_int(req.pStmt, 3, in->rate);
            sqlite3_bind_blob(req.pStmt, 4, in->pcrArray, sizeof(float) * in->samples, NULL);
            sqlite3_bind_int(req.pStmt, 5, in->samples);
            sqlite3_bind_int(req.pStmt, 6, currTime + j);
            int errCode = sqlite3_step(req.pStmt);
            ss.str("");
        }
        //double timeStampCycle = timer.stop();
        //timer.start();
        sqlite3_exec(m_pDb, "END TRANSACTION", NULL, NULL, NULL);
        //double timeStampEnd = timer.stop();
        //fs << timeStampBegin << ", " << timeStampCycle << ", " << timeStampEnd << ", "; 
    }
}

void Database::addToOutputT(time_t currTime, const LogSample* samples, uint32_t count)
{
    if (count == 0)
        return;
    std::stringstream ss;
    Timer timer;
    for (uint32_t i = 0; i < DS_OUT_TOTAL; i++)
    {
        DataSource DS = static_cast<DataSource>(i + DS_OUT_BASE);

        if (!isDataSourceSupported(DS))
            continue;

        //timer.start();
        sqlite3_exec(m_pDb, "BEGIN TRANSACTION", NULL, NULL, NULL);
        //double timeStampBegin = timer.stop();
        //timer.start();
        for (uint32_t j = 0; j < count; j++)
        {
            ss << "INSERT INTO " << getTableName(DS) << "(delayFactor, rate, pcrArray,\
										samples, time)  VALUES(?,?,?,?,?);";
            LogSample sample = samples[j];
            const OutputData* out = static_cast<OutputData*>(getSample(sample, DS));
            SQLiteRequest req(m_pDb, ss.str());
            sqlite3_bind_double(req.pStmt, 1, out->delayFactor);
            sqlite3_bind_int(req.pStmt, 2, out->rate);
            sqlite3_bind_blob(req.pStmt, 3, out->pcrArray, sizeof(float) * out->samples, NULL);
            sqlite3_bind_int(req.pStmt, 4, out->samples);
            sqlite3_bind_int(req.pStmt, 5, currTime + j);
            int errCode = sqlite3_step(req.pStmt);
            ss.str("");
        }
        //double timeStampCycle = timer.stop();
        //timer.start();
        sqlite3_exec(m_pDb, "END TRANSACTION", NULL, NULL, NULL);
        //double timeStampEnd = timer.stop();
        //fs << timeStampBegin << ", " << timeStampCycle << ", " << timeStampEnd << ", \n";
    }
}

void Database::createEmptyDb()
{
    std::string dbEmptyFileName = m_dbFileName;
    dbEmptyFileName.erase(dbEmptyFileName.size() - 3, 3);
    dbEmptyFileName += "Empty.db";
    m_dbEmptyFileName = dbEmptyFileName;
    open(m_dbEmptyFileName, false);
    createTables();
    close();
}

void* Database::getSample(LogSample& sample, DataSource source)
{
    switch (source)
    {
    default:
    case DS_IN_HP1: return &sample.hp1;
    case DS_IN_LP1: return &sample.lp1;
    case DS_IN_HP2: return &sample.hp2;
    case DS_IN_LP2: return &sample.lp2;
    case DS_OUT_HP: return &sample.hpOut;
    case DS_OUT_LP: return &sample.lpOut;
    }
}

DBData Database::getInputData(DataSource source, LogSample* samples, uint32_t count, sqlite3_stmt* pStmt)
{
    time_t currTime;
    DBData dbData;
    dbData.counter = 0;
    time_t& startTime = dbData.startTime;
    uint32_t& counter = dbData.counter;
    bool firstStep = true;
    while (sqlite3_step(pStmt) == SQLITE_ROW && counter < count)
    {
        if (firstStep)
        {
            startTime = sqlite3_column_int(pStmt, 5);
            firstStep = false;
        }
        else
        {
            currTime = sqlite3_column_int(pStmt, 5);
            if (startTime + counter != currTime)
            {
                counter = 0;
                return dbData;
            }
        }
        LogSample& sample = samples[counter];
        InputData* in = static_cast<InputData*>(getSample(sample, source));
        in->delayFactor = sqlite3_column_double(pStmt, 0);
        in->mediaLossRate = sqlite3_column_int(pStmt, 1);
        in->rate = sqlite3_column_int(pStmt, 2);
        in->samples = sqlite3_column_int(pStmt, 4);
        memcpy(in->pcrArray, (float*)sqlite3_column_blob(pStmt, 3),
            sizeof(float) * in->samples);
        counter++;
    }
    return dbData;
}

DBData Database::getOutputData(DataSource source, LogSample* samples, uint32_t count, sqlite3_stmt* pStmt)
{
    time_t currTime;
    DBData dbData;
    dbData.counter = 0;
    time_t& startTime = dbData.startTime;
    uint32_t& counter = dbData.counter;
    bool firstStep = true;
    while (sqlite3_step(pStmt) == SQLITE_ROW && counter < count)
    {
        if (firstStep)
        {
            startTime = sqlite3_column_int(pStmt, 4);
            firstStep = false;
        }
        else
        {
            currTime = sqlite3_column_int(pStmt, 4);
            if (startTime + counter != currTime)
            {
                counter = 0;
                return dbData;
            }
        }
        LogSample& sample = samples[counter];
        OutputData* out = static_cast<OutputData*>(getSample(sample, source));
        out->delayFactor = sqlite3_column_double(pStmt, 0);
        out->rate = sqlite3_column_int(pStmt, 1);
        out->samples = sqlite3_column_int(pStmt, 3);
        memcpy(out->pcrArray, (float*)sqlite3_column_blob(pStmt, 2),
            sizeof(float) * out->samples);
        counter++;
    }
    return dbData;
}

uint32_t Database::createLogHeaderCSV(char* buffer, uint32_t bufferSize)
{
    if (!buffer || !bufferSize)
        return 0;

    int result = snprintf(buffer, bufferSize, L_FMT_HEAD,
        "Time",
        "Active Input",
#ifdef HIER_MODE_SUPPORTED
        "HP Rate 1", "HP DF 1", "HP MLR 1",
        "LP Rate 1", "LP DF 1", "LP MLR 1",
        "HP Rate 2", "HP DF 2", "HP MLR 2",
        "LP Rate 2", "LP DF 2", "LP MLR 2",
        "HP Out Rate", "HP Out DF",
        "LP Out Rate", "LP Out DF"
#else
        "Rate 1", "DF 1", "MLR 1",
        "Rate 2", "DF 2", "MLR 2",
        "Out Rate", "Out DF"
#endif
        );

    return (result > 0) ? static_cast<uint32_t>(result) : 0;
}

uint32_t Database::createLogEntryCSV(char* buffer, uint32_t bufferSize, time_t entryTime,
    const LogSample& sample)
{
    if (!buffer || !bufferSize)
        return 0;
    int result = 0;

    std::stringstream ss;
    ss << entryTime;

    const InputData& hp1 = sample.hp1;
    const InputData& hp2 = sample.hp2;
    const OutputData& hpOut = sample.hpOut;
#ifdef HIER_MODE_SUPPORTED
    const InputData& lp1 = sample.lp1;
    const InputData& lp2 = sample.lp2;
    const OutputData& lpOut = sample.lpOut;
#endif

    result += snprintf(buffer, bufferSize, L_FMT_DATA, ss.str().c_str(), sample.activeInput,
#ifdef HIER_MODE_SUPPORTED
        hp1.rate, hp1.delayFactor, hp1.mediaLossRate,
        lp1.rate, lp1.delayFactor, lp1.mediaLossRate,
        hp2.rate, hp2.delayFactor, hp2.mediaLossRate,
        lp2.rate, lp2.delayFactor, lp2.mediaLossRate,
        hpOut.rate, hpOut.delayFactor,
        lpOut.rate, lpOut.delayFactor
#else
        hp1.rate, hp1.delayFactor, hp1.mediaLossRate,
        hp2.rate, hp2.delayFactor, hp2.mediaLossRate,
        hpOut.rate, hpOut.delayFactor
#endif
        );
    return (result > 0) ? static_cast<uint32_t>(result) : 0;
}

void Database::changePackSizeDEBUG(uint32_t packSize)
{
    m_transPackSize = packSize;
}

