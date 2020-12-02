#include <atomic>
#include <jvmti.h>
#include <string>

#include "agentOptions.hpp"
#include "infra.hpp"
#include "json.hpp"
#include "exception.hpp"

using namespace std;
using json = nlohmann::json;

std::atomic<bool> backTraceEnabled {true};
std::atomic<int> exceptionSampleCount {0};
std::atomic<int> exceptionSampleRate {1};


void setExceptionSampleRate(int rate) {
    /* Set sampling rate and enable/disable backtrace */
    if (rate > 0) {
        setExceptionBackTrace(true);
        exceptionSampleRate = rate;
    } else {
        setExceptionBackTrace(false);
    }
}


void setExceptionBackTrace(bool val){
    /* Enables or disables the stack trace option */
    backTraceEnabled = val;
    return;
}


JNIEXPORT void JNICALL Exception(jvmtiEnv *jvmtiEnv,
            JNIEnv* jniEnv,
            jthread thread,
            jmethodID method,
            jlocation location,
            jobject exception,
            jmethodID catch_method,
            jlocation catch_location) {

    json jdata;
    jvmtiError err;
    jclass klass;
    char *fileName;
    int lineCount, lineNumber;
    jvmtiLineNumberEntry *lineTable;
    char buffer[20];
    char *methodName;

    static int numExceptions = 0; // number of total exceptions recorded
    numExceptions++;

    // First get exception address
    char ptrStr[] = "%p";
    sprintf(buffer, ptrStr, exception);
    string exceptionStr(buffer);
    jdata["exceptionAddress"] = (char*)exceptionStr.c_str();

    /* Get method name */
    err = jvmtiEnv->GetMethodName(method, &methodName, NULL, NULL);
    if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Name.\n")) { 
        jdata["callingMethod"] = (char *)methodName;  // record calling method
    } 

    /* Get line number */
    err = jvmtiEnv->GetLineNumberTable(method, &lineCount, &lineTable);
    if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Line Number Table.\n")) { // Find line
        lineNumber = lineTable[0].line_number;
        for (int i = 1; i < lineCount; i++) {
            if (location < lineTable[i].start_location) {
                break;
            } else {
                lineNumber = lineTable[i].line_number;
            }
        }
        jdata["callingMethodLineNumber"] = lineNumber;  // record line number of calling method
    } 

    /* Get jclass object of calling method*/
    err = jvmtiEnv->GetMethodDeclaringClass(method, &klass);
    if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Declaring Class.\n")) {
        /* Get source file name */
        err = jvmtiEnv->GetSourceFileName(klass, &fileName);
        if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Source File Name.\n")) {
            jdata["callingMethodSourceFile"] = fileName;
        }
    }

    err = jvmtiEnv->Deallocate((unsigned char*)fileName);
    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate fileName.\n");
    err = jvmtiEnv->Deallocate((unsigned char*)methodName);
    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate methodName.\n");
    err = jvmtiEnv->Deallocate((unsigned char*)lineTable);
    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate lineTable.\n");

    // Get information from stack
    if (backTraceEnabled) { // only run when backtrace is enabled
        if (exceptionSampleCount % exceptionSampleRate == 0) {
            int numFrames = 5;
            jvmtiFrameInfo frames[numFrames];
            jint count;
            auto jMethods = json::array();

            err = jvmtiEnv->GetStackTrace(thread, 0, numFrames,
                                        frames, &count);
            if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Stack Trace.\n") && count >= 1) {
                json jMethod;
                for (int i = 0; i < count; i++) {
                    /* Get method name */
                    err = jvmtiEnv->GetMethodName(frames[i].method,
                                        &methodName, NULL, NULL);
                    if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Name.\n")) {
                        jMethod["methodName"] = methodName;
                    }

                    err = jvmtiEnv->GetLineNumberTable(frames[i].method, &lineCount, &lineTable);
                    if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Line Number Table.\n")) { // Find line
                        lineNumber = lineTable[0].line_number;
                        for (int j = 1; j < lineCount; j++) {
                            if (frames[i].location < lineTable[i].start_location) {
                                break;
                            } else {
                                lineNumber = lineTable[i].line_number;
                            }
                        }

                        jMethod["methodLineNumber"] = lineNumber;
                    }

                    /* Get jclass object of calling method*/
                    err = jvmtiEnv->GetMethodDeclaringClass(frames[i].method, &klass);
                    if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Declaring Class.\n")) {
                        /* Get file name */
                        err = jvmtiEnv->GetSourceFileName(klass, &fileName);
                        if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Source File Name.\n")) {
                            jMethod["fileName"] = fileName;
                        }
                    }

                    jMethods.push_back(jMethod);

                    err = jvmtiEnv->Deallocate((unsigned char*)fileName);
                    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate fileName.\n");
                    err = jvmtiEnv->Deallocate((unsigned char*)methodName);
                    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate methodName.\n");
                    err = jvmtiEnv->Deallocate((unsigned char*)lineTable);
                    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate lineTable.\n");
                }
            }

            jdata["backtrace"] = jMethods;
        }

        exceptionSampleCount++;
    }

    jdata["numExceptions"] = numExceptions;

    sendToServer(jdata.dump());
}
