// TODO: object count
#include <jvmti.h>
#include <string.h>

#include "agentOptions.hpp"
#include "infra.hpp"
#include "json.hpp"
#include "server.hpp"
#include "infra.hpp"

#include <iostream>
#include <chrono>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <atomic>

using json = nlohmann::json;
using namespace std::chrono;

std::atomic<bool> objAllocBackTraceEnabled {true};
std::atomic<int> objAllocSampleCount {0};
std::atomic<int> objAllocSampleRate {1};

// Enables or disables the back trace option if sampleRate == 0
void setObjAllocBackTrace(bool val){
    objAllocBackTraceEnabled = val;
    return;
}

// to turn off backTrace, set sampleRate to 0
void setObjAllocSampleRate(int rate) {
    if (rate > 0) {
        objAllocBackTraceEnabled = true;
        objAllocSampleRate = rate;
    } else {
        objAllocBackTraceEnabled = false;
    }
    return;
}

/*** retrieves object type name, size (in bytes), allocation rate (bytes/microsec),
 *      and backtrace for every nth sample (if enabled)                             ***/
JNIEXPORT void JNICALL VMObjectAlloc(jvmtiEnv *jvmtiEnv, 
                        JNIEnv* env, 
                        jthread thread, 
                        jobject object, 
                        jclass object_klass, 
                        jlong size) {
    jvmtiError err;

    json jObj;
    char *classType;
    auto start = steady_clock::now();


    /*** get information about object ***/
    err = jvmtiEnv->GetClassSignature(object_klass, &classType, NULL);
    if (classType != NULL && check_jvmti_error(jvmtiEnv, err, "Unable to retrive Object Class.\n")) {
        jObj["objType"] = classType;
        jObj["size"] = (jint)size;
    }

    /*** get information about backtrace at object allocation sites if enabled***/
    /*** retrieves method names and line numbers, and declaring class name and signature ***/
    if (objAllocBackTraceEnabled) {
        if (objAllocSampleCount % objAllocSampleRate == 0){
            int numFrames = 10;
            jvmtiFrameInfo frames[numFrames];
            jint count;
            int i;
            // int sze = 0;
            auto jMethods = json::array();
            err = jvmtiEnv->GetStackTrace(NULL, 0, numFrames, frames, &count);
            if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Stack Trace.\n") && count >= 1) {
                for (i = 0; i < count; i++) {
                    char *methodName;
                    char *methodSignature;
                    char *declaringClassName;
                    jclass declaring_class;
                    jint entry_count_ptr;
                    jvmtiLineNumberEntry* table_ptr;
                    // jlocation start_loc_ptr;
                    // jlocation end_loc_ptr;

                    json jMethod;
                    jMethod["methodNum"] = i;
                    err = jvmtiEnv->GetMethodName(frames[i].method, &methodName, &methodSignature, NULL);
                    if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Name.\n")) {
                        jMethod["methodName"] = methodName;
                        jMethod["methodSignature"] = methodSignature;
                    }
                    err = jvmtiEnv->GetMethodDeclaringClass(frames[i].method, &declaring_class);
                    if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Declaring Class.\n")) {
                        err = jvmtiEnv->GetClassSignature(declaring_class, &declaringClassName, NULL);
                        if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Declaring Class Signature.\n")) {
                            jMethod["methodClass"] = declaringClassName;
                        } 
                    }
                    // err = jvmtiEnv->GetMethodLocation(method, &start_loc_ptr, &end_loc_ptr);
                    // if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Location.\n")) {
                    // }
                    err = jvmtiEnv->GetLineNumberTable(frames[i].method, &entry_count_ptr, &table_ptr);
                    if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Line Number Table.\n")) {
                        jMethod["methodLineNum"] = table_ptr[i].line_number;
                    }
                    
                    jMethods.push_back(jMethod);

                    err = jvmtiEnv->Deallocate((unsigned char*)methodSignature);
                    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate methodSignature.\n");
                    err = jvmtiEnv->Deallocate((unsigned char*)methodName);
                    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate methodName.\n");
                    err = jvmtiEnv->Deallocate((unsigned char*)declaringClassName);
                    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate declaringClassName.\n");
                    err = jvmtiEnv->Deallocate((unsigned char*)table_ptr);
                    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate table_ptr.\n");
                }
            } 
                
            jObj["objBackTrace"] = jMethods;
            jObj["objBackTraceSampleNum"] = objAllocSampleCount.load();
        }
        objAllocSampleCount++;
    }


    /*** calculate time taken in microseconds and calculate rate ***/
    auto end = steady_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    float rate = (float)size/duration;
    jObj["objAllocRate"] = rate;

    err = jvmtiEnv->Deallocate((unsigned char*)classType);
    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate classType.\n");

    json j;
    j["object"] = jObj; 
    std::string s = j.dump(2, ' ', true);
    // printf("\n%s\n", s.c_str());
    sendToServer(s);

}