#pragma once
#include "Utils.h"
#include "sqlite3.h"
#include <iostream>
#include <sstream>
#include <mutex>
#include "Defs.h"
//#include <variant>

#define DEBUG


struct LogSample
{
	// обязательные поля
	uint32_t activeInput = 1; // 1=primary, 2=secondary
	InputData hp1;
	InputData hp2;
	OutputData hpOut;
	// опциональные поля, не всегда нужны (это можно)
	InputData lp1;
	InputData lp2;
	OutputData lpOut;	
};


struct DBData
{
	time_t startTime;
	uint32_t counter;
};


class Database
{
	
private:
	sqlite3* m_pDb;	
	uint32_t m_atomicDumpSize;
	std::mutex m_DbMutex;
	uint32_t m_transPackSize;
	uint32_t m_limit;
	std::string m_dbFileName;
	std::string m_dbEmptyFileName;
public:
	 
	Database(const std::string& fileName, bool bRecreate);
	~Database();
	bool open(const std::string& fileName, bool bRecreate);
	void close();
	void createTables();

	time_t getStartTime(); // timestamp of the first sample
	uint32_t getTotalSamples(); // total number of samples
	bool verifyIntegrity();	
	void add(time_t startTime, const LogSample* samples, uint32_t count);
	void addT(time_t startTime, const LogSample* samples, uint32_t count);
	uint32_t get(LogSample* samples, uint32_t count, time_t startTime);
	void clear();
	void clearFake(std::ofstream& fs);
	bool dump(const std::string& fileName);
	void changePackSizeDEBUG(uint32_t packSize); // TODO back to private
private:
	
	bool deleteFirstSample();
	bool deleteFirstNSamples(uint32_t n);

	uint32_t internalGet(LogSample* samples, uint32_t count, time_t& startTime);	
	void addToInput(time_t currTime, const LogSample& sample);
	void addToOutput(time_t currTime, const LogSample& sample);
	void addToInputT(time_t currTime, const LogSample* samples, uint32_t count);
	void addToOutputT(time_t currTime, const LogSample* samples, uint32_t count);
	void createEmptyDb();	
	uint32_t internalGetTotalSamples(); // total number of samples	
	void* getSample(LogSample& sample, DataSource source);
	//InputData* getSampleIn(LogSample& sample, DataSource source);
	//OutputData* getSampleOut(LogSample& sample, DataSource source);
	DBData getInputData(DataSource source, LogSample* samples, uint32_t count, sqlite3_stmt* pStmt);
	DBData getOutputData(DataSource source, LogSample* samples, uint32_t count, sqlite3_stmt* pStmt);

	/// create log header (CSV)
	static uint32_t createLogHeaderCSV(char* buffer, uint32_t bufferSize);
	/// create log entry (CSV)
	static uint32_t createLogEntryCSV(char* buffer, uint32_t bufferSize, time_t entryTime,
									  const LogSample& sample);

	//static bool saveLogCSV(FILE* f, time_t startTime, const LogData& data);		
};
