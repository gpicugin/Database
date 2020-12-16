#include "Database.h"
#include "Timer.h"
#define DEBUG
//#include <windows.h> 



void fillpcrArray(float(/*&*/pcrArray)[maxSamples], uint8_t samples)
{
	for (uint8_t i = 0; i < samples; i++)
	{
		pcrArray[i] = Random::getFloatRange(0.0, 10.0);
	}
}

template <typename T>
void generateRandomData(T& t)
{
	uint32_t nominalRate = Random::getIntRange(4e6, 40e6);
	uint32_t rateDeviation = Random::getIntRange(2, 8) * nominalRate / 100;

	t.delayFactor = Random::getFloatRange(0.000050, 0.006000);
	t.rate = Random::getIntRange(nominalRate - rateDeviation, nominalRate + rateDeviation);
	t.samples = Random::getIntRange(1, 10);
	fillpcrArray(t.pcrArray, t.samples);
}

void fillRandom(InputData& in)
{
	generateRandomData(in);
	in.mediaLossRate = Random::getIntRange(0, 3);
}

void fillRandom(OutputData& out)
{
	generateRandomData(out);
}

time_t fillRandomToInputOutput(Database& db, uint32_t size, time_t startTime = time(NULL))
{
	uint32_t nominalRate = Random::getIntRange(4e6, 40e6);
	uint32_t rateDeviation = Random::getIntRange(2, 8) * nominalRate / 100;
	LogSample* samples = new LogSample[size];

	uint32_t i = 0;
	while (i < size)
	{
		LogSample& sample = samples[i];

		// input hp1
		fillRandom(sample.hp1);
		fillRandom(sample.hp2);
		fillRandom(sample.hpOut);

#ifdef HIER_MODE_SUPPORTED
		fillRandom(sample.lp1);
		fillRandom(sample.lp2);
		fillRandom(sample.lpOut);
#endif

		i++;
	}
	db.addT(startTime, samples, size);
	return startTime;
}

void dumpThreadFunc(Database& database)
{
	std::stringstream ss;
	char timeStr[32];
	time_t local = time(NULL);
	strftime(timeStr, sizeof(timeStr), "%F_%H-%M-%S", localtime(&local));
	std::string str(timeStr);
	ss << "dump_" << str << ".csv";
	Timer timer;
	std::cout << "DUMP HAS BEEN STARTED\n";
	timer.start();
	bool result = database.dump(ss.str());	
	double timeStamp = timer.stop();
	if (result)
		std::cout << "DUMP HAS BEEN DONE\n";
	else
		std::cout << "DUMP HAS BEEN FAILED\n";
	std::cout << timeStamp << "\n";
}

uint32_t tableEntry(char* buffer, uint32_t bufferSize, double time)
{
	if (!buffer || !bufferSize)
		return 0;

	int result = 0;

	std::stringstream ss;
	ss << time;
	char ch = ',';
	std::string str = ss.str();
	str.replace(str.find("."), 1, ",");
	//str[1] = ch;

	result += snprintf(buffer, bufferSize, "%s\n", str.c_str());
	return (result > 0) ? static_cast<uint32_t>(result) : 0;

}

void getTesting(Database& database, std::ofstream& fs)
{
	Timer timer;
	uint32_t size = 600;
	uint32_t startTime1 = database.getStartTime();
	LogSample* samples = new LogSample[size];
	double average = 0;
	for (uint32_t i = 0; i < 10; i++)
	{
		std::cout << i << " - GET\n";
		for (int j = 0; j < 10; j++)
		{
			
			timer.start();
			database.get(samples, size, startTime1 + j * size);
			double timeStamp = timer.stop();
			fs << timeStamp << ", ";
		}	
		fs << "\n";
	}
}

void addTesting(Database& database, uint32_t size, uint32_t begin, uint32_t end, uint32_t step, std::ofstream& fs)
{
	fs << "iteration, "
		<< "hp1Begin, " << "hp1Cycle, " << "hp1End, "
		<< "hp2Begin, " << "hp2Cycle, " << "hp2End, "
		<< "hpOutBegin, " << "hpOutCycle, " << "hpOutEnd, \n";
		
	time_t startTime = time(NULL);

	LogSample* samples = new LogSample[size];

	for (int j = 0; j < size; j++) {

		fillRandom(samples[j].hp1);
		fillRandom(samples[j].hp2);
		fillRandom(samples[j].hpOut);

#ifdef HIER_MODE_SUPPORTED
		fillRandom(samples[j].lp1);
		fillRandom(samples[j].lp2);
		fillRandom(samples[j].lpOut);
#endif		
	}
	uint32_t packSize = 0;
	uint32_t shift = 0;
	
	for (uint32_t i = begin; i <= end; i += step)
	{
		std::cout << i << " ITERATION \n";
		uint32_t packSize = i;
		database.changePackSizeDEBUG(packSize);
		for (uint32_t j = 0; j < 10; j++) // цикл по пачкам
		{
			fs << i << ", ";
			database.addT(startTime + shift, samples + shift, packSize);	
			shift += packSize;
		}			
	}
	
	fs << "\n";
}

void clearTesting(Database& database, std::ofstream& fs)
{
	Timer timer;
	timer.start();
	database.clearFake(fs);
	double timeStamp = timer.stop();
	fs << timeStamp << "\n";
}

int main()
{
    Timer timer;
    std::string s = "test.csv";
    std::ofstream fs;
    fs.open(s);
    Random::initialize(); // TODO взять стандартный рандом
    uint32_t dbSize1 = 5000;
    uint32_t maxDBSize = 1209600;
    std::string dbName = "dbTest.db";
    Database database(dbName, true);    
    //database.clear();
    //database.createTables();
    uint32_t total = database.getTotalSamples();

    std::cout << "TOTAL OF ENTRIES BEFORE FILL IS " << total << "\n";

    uint32_t startTime = fillRandomToInputOutput(database, dbSize1);

    std::cout << "TIME FROM BASE " << database.getStartTime() << "\n";
    std::cout << "TIME FROM PROGRAMM " << startTime << "\n";

    total = database.getTotalSamples();

    std::cout << "TOTAL OF ENTRIES AFTER FILL IS " << total << "\n";

    bool f = database.verifyIntegrity();

    std::cout << "IS BASE OK? " << (f ? "ok" : "NOT OK") << '\n';

    if (!f)
    {
        getchar();
        return 0;
    }
    bool flag = true;
    int counter = 0;

    const double updateInterval = 1.0; // 1s
    double updateTime = 0;
    double timeToDump = 15.0;
    double timeToEnd = 30.0;
    time_t startTime1 = database.getStartTime();
    std::cout << startTime1 << "\n";
    timer.start();
    while (1)
    {
        double dt = timer.getElapsed();

        updateTime += dt;
        if (updateTime >= updateInterval)
        {
            updateTime -= updateInterval;

            if (counter % 5 == 0)
                std::cout << counter << '\n';

            fillRandomToInputOutput(database, 1, startTime1 + dbSize1 + counter);
            counter++;
        }

        timeToDump -= dt;
        if ((timeToDump <= 0) && flag)
        {
            flag = false;
            std::thread dump_thread(dumpThreadFunc, std::ref(database));
            dump_thread.detach();
            //break;
        }

        timeToEnd -= dt;
        if (timeToEnd <= 0)
        {
            timer.stop();
            std::cout << counter << "\nEND OF LOOP\n";
            break;
        }
    }
    time_t strt = database.getStartTime();
    std::cout << strt << "\n"; 
    fs.close();
    return 0;
}

