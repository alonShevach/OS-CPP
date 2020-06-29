#include <atomic>
#include "MapReduceFramework.h"
#include "Barrier.h"
#include <iostream>

struct ShuffleThreadContext;

/**
 * a struct
 *  which includes all the parameters which are relevant to the job
 */
struct JobContext
{
    const MapReduceClient &client;
    const InputVec &inputVec;
    OutputVec &outputVec;
    ShuffleThreadContext *stc;
    IntermediateMap intermediateMap;
    Barrier mapBarrier;
    int multiThreadLevel;
    std::atomic<uint32_t> atomic_counter;
    std::atomic<uint32_t> shuffleCounter;
    std::atomic<uint32_t> threadCounter;
    std::atomic<uint64_t> emitCounter;
    std::atomic<uint64_t> atomicVar;
    pthread_mutex_t mapMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t outputMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t *mutices;
    pthread_t *threads;

    JobContext(const MapReduceClient &c, const InputVec &i, OutputVec &o, int map)
            : client(c), inputVec(i), outputVec(o), mapBarrier(Barrier(map))
    {}
};

/**
 * a struct that defines the thread context.
 */
struct ThreadContext
{
    int threadId;
    JobContext *job;
    std::vector<IntermediatePair> *intermediateVector;
};

/**
 * a struct used for the shuffle part.
 */
struct ShuffleThreadContext
{
    ThreadContext *contexts;
    JobContext *job;
};

/**
 * This function will produce intermediate pairs of <K3, V3>.
 * @param key K2 type key
 * @param value K2 type value.
 * @param context the current job.
 */
void emit2(K2 *key, V2 *value, void *context)
{
    auto tc = (ThreadContext *) context;
    if (pthread_mutex_lock(&tc->job->mutices[tc->threadId]) != 0)
    {
        std::cerr << "system error: Failed to lock mutex" << std::endl;
        exit(1);
    }
    tc->intermediateVector->push_back(IntermediatePair(key, value));
    if (pthread_mutex_unlock(&tc->job->mutices[tc->threadId]) != 0)
    {
        std::cerr << "system error: Failed to unlock mutex" << std::endl;
        exit(1);
    }
    tc->job->emitCounter.fetch_add(1);
}

/**
 * this function  will push the given pair<K3, V3> to the output vector.
 * @param key K3 type key
 * @param value K3 type value.
 * @param context the current job.
 */
void emit3(K3 *key, V3 *value, void *context)
{
    auto jc = (JobContext *) context;
    if (pthread_mutex_lock(&jc->outputMutex) != 0)
    {
        std::cerr << "system error: Failed to lock mutex" << std::endl;
        exit(1);
    }
    jc->outputVec.push_back(OutputPair(key, value));
    if (pthread_mutex_unlock(&jc->outputMutex) != 0)
    {
        std::cerr << "system error: Failed to unlock mutex" << std::endl;
        exit(1);
    }
}

void reduce(JobContext *job)
{
    while (true)
    {
        if (pthread_mutex_lock(&job->mapMutex) != 0)
        {
            std::cerr << "system error: Failed to lock mutex" << std::endl;
            exit(1);
        }
        if (job->intermediateMap.empty())
        {
            if (pthread_mutex_unlock(&job->mapMutex) != 0)
            {
                std::cerr << "system error: Failed to unlock mutex" << std::endl;
                exit(1);
            }
            break;
        }
        auto pairPointer = job->intermediateMap.begin();
        auto pair = *pairPointer;
        job->intermediateMap.erase(pairPointer);
        if (pthread_mutex_unlock(&job->mapMutex) != 0)
        {
            std::cerr << "system error: Failed to unlock mutex" << std::endl;
            exit(1);
        }
        job->client.reduce(pair.first, pair.second, job);
        job->atomicVar.fetch_add(1);
    }
    job->threadCounter.fetch_add(1);
}

/**
 * In this phase each thread reads pairs of (k1,v1) from the input vector and calls the map function on
 * each of them
 * @param context the current job
 * @return nullptr
 */
void *map(void *context)
{
    auto tc = (ThreadContext *) context;
    while (true)
    {
        int index = (tc->job->atomic_counter.fetch_add(1));
        if (index >= (int) tc->job->inputVec.size())
        {
            break;
        }
        tc->job->client.map(tc->job->inputVec[index].first, tc->job->inputVec[index].second, tc);
        tc->job->atomicVar.fetch_add(1);
    }
    (tc->job->shuffleCounter.fetch_add(1));
    tc->job->mapBarrier.barrier();
    reduce(tc->job);
    return nullptr;
}

/**
 * this function updates the job's atomicVar atomic counter
 * @param job the current job
 */
void updateAtomicVar(JobContext *job)
{
    uint64_t sizeSum = 0;
    for (auto it = job->intermediateMap.begin(); it != job->intermediateMap.end(); ++it)
    {
        sizeSum++;
    }
    job->atomicVar = (sizeSum << 31) + ((uint64_t) 3 << 62);
}

/**
 * This phase reads the Map phase output and combines them into a single IntermediateMap
 * @param context the current job
 * @return nullptr
 */
void *shuffle(void *context)
{
    bool isShuffle = false;
    auto stc = (ShuffleThreadContext *) context;
    int counter = 0;
    int shuffle = 2;
    while (shuffle > 0)
    {
        for (int i = 0; i < stc->job->multiThreadLevel - 1; ++i)
        {
            if (pthread_mutex_lock(&stc->job->mutices[i]) != 0)
            {
                std::cerr << "system error: Failed to lock mutex" << std::endl;
                exit(1);
            }
            while (!stc->contexts[i].intermediateVector->empty())
            {
                if ((int) stc->job->shuffleCounter == stc->job->multiThreadLevel - 1 && !isShuffle)
                {
                    isShuffle = true;
                    stc->job->atomicVar = ((uint64_t) 2 << 62) + (stc->job->emitCounter << 31) + counter;
                }
                auto pair = stc->contexts[i].intermediateVector->back();
                stc->contexts[i].intermediateVector->pop_back();
                stc->job->intermediateMap[pair.first].push_back(pair.second);
                counter++;
                if (isShuffle)
                {
                    stc->job->atomicVar.fetch_add(1);
                }
            }
            if (pthread_mutex_unlock(&stc->job->mutices[i]) != 0)
            {
                std::cerr << "system error: Failed to unlock mutex" << std::endl;
                exit(1);
            }
        }
        if ((int) stc->job->shuffleCounter == stc->job->multiThreadLevel - 1)
        {
            --shuffle;
        }
    }
    updateAtomicVar(stc->job);
    stc->job->mapBarrier.barrier();
    reduce(stc->job);
    return nullptr;
}

/**
 * This function starts running the MapReduce algorithm (with several threads) and returns a JobHandle
 * @param client the MapReduceClient
 * @param inputVec <K1, V1> input vector.
 * @param outputVec The vector to output the results to
 * @return JobHandle job.
 */
JobHandle startMapReduceJob(const MapReduceClient &client,
                            const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel)
{
    auto *job = new JobContext(client, inputVec, outputVec, multiThreadLevel);
    job->atomicVar = (uint64_t) inputVec.size() << 31;
    job->threadCounter = 0;
    job->multiThreadLevel = multiThreadLevel;
    job->threads = new pthread_t[multiThreadLevel];
    auto *stc = new ShuffleThreadContext();
    job->stc = stc;
    stc->job = job;
    stc->contexts = new ThreadContext[multiThreadLevel - 1];
    job->atomic_counter = 0;
    job->shuffleCounter = 0;
    job->emitCounter = 0;
    job->mutices = new pthread_mutex_t[multiThreadLevel - 1];
    for (int i = 0; i < multiThreadLevel - 1; ++i)
    {
        stc->contexts[i] = {i, job, new std::vector<IntermediatePair>};
        job->mutices[i] = PTHREAD_MUTEX_INITIALIZER;
    }
    job->atomicVar.fetch_add((uint64_t) 1 << 62);
    for (int i = 0; i < multiThreadLevel - 1; ++i)
    {
        if (pthread_create(job->threads + i, nullptr, map, stc->contexts + i) != 0)
        {
            std::cerr << "system error: Failed to create thread" << std::endl;
            exit(1);
        }
    }
    pthread_create(job->threads + multiThreadLevel - 1, nullptr, shuffle, stc);
    return job;
}

/**
 *  a function gets the job handle returned by startMapReduceFramework and
 *  waits until it is finished.
 * @param job the current job.
 */
void waitForJob(JobHandle job)
{
    auto context = (JobContext *) job;
    while (context->threadCounter != context->multiThreadLevel)
    {
    }
}

/**
 *  this function gets a job handle and updates the state of the job into the given
 *  JobState struct.
 * @param job the current job.
 * @param state the state to update according to the job.
 */
void getJobState(JobHandle job, JobState *state)
{
    auto context = (JobContext *) job;
    uint64_t var = context->atomicVar;
    state->stage = stage_t((var) >> 62);
    float taskSize = ((var) & (((uint64_t) -1 >> 33) << 31)) >> 31;
    float progSize = ((var) << 33) >> 33;
    state->percentage = (progSize / taskSize) * 100;
}

/**
 * Releasing all resources of a job. You should prevent releasing resources
 * before the job is finished. After this function is called the job handle will be invalid.
 * @param job the current job.
 */
void closeJobHandle(JobHandle job)
{
    waitForJob(job);
    auto jc = (JobContext *) job;
    for (int i = 0; i < jc->multiThreadLevel - 1; ++i)
    {
        delete (jc->stc->contexts[i]).intermediateVector;
        if (pthread_join(jc->threads[i], NULL) != 0)
        {
            std::cerr << "system error: Failed to join thread" << std::endl;
            exit(1);
        }
        if (pthread_mutex_destroy(&jc->mutices[i]) != 0)
        {
            std::cerr << "system error: Failed to destroy mutex" << std::endl;
            exit(1);
        }
    }
    if (pthread_join(jc->threads[jc->multiThreadLevel - 1], NULL) != 0)
    {
        std::cerr << "system error: Failed to join thread" << std::endl;
        exit(1);
    }
    if (pthread_mutex_destroy(&jc->mapMutex) != 0 || pthread_mutex_destroy(&jc->outputMutex) != 0)
    {
        std::cerr << "system error: Failed to destroy mutex" << std::endl;
        exit(1);
    }
    delete[] jc->stc->contexts;
    delete jc->stc;
    delete[] jc->threads;
    delete[] jc->mutices;
    delete jc;
}

