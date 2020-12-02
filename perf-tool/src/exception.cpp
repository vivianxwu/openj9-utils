/*******************************************************************************
 * Copyright (c) 2020, 2020 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

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
std::atomic<int> exceptionSampleCount {1};
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
    int numExceptions;

    /* Get number of exceptions recorded and increment */
    numExceptions = atomic_fetch_add(&exceptionSampleCount, 1);
    jdata["numExceptions"] = numExceptions;

    /* Get exception address */
    sprintf(buffer, "%p", exception);
    jdata["exceptionAddress"] = buffer;

    /* Get method name */
    err = jvmtiEnv->GetMethodName(method, &methodName, NULL, NULL);
    if (err == JVMTI_ERROR_NONE) {
        jdata["callingMethod"] = methodName;  // record calling method
    }
    err = jvmtiEnv->Deallocate((unsigned char*)methodName);
    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate methodName");

    /* Get line number */
    err = jvmtiEnv->GetLineNumberTable(method, &lineCount, &lineTable);  // returns table of source line num entries
    if (err == JVMTI_ERROR_NONE) { // Find line
        lineNumber = lineTable[0].line_number;  // initially set line number to first line number of lineTable
        for (int i = 1; i < lineCount; i++) {
            // iterates through lineTable until the location from where the calling method was called is reached
            if (location < lineTable[i].start_location) {
                // location of line in calling method code has been passed, exit for loop
                break;
            } else {
                // location not passed yet; update line number
                lineNumber = lineTable[i].line_number;
            }
        }
        jdata["callingMethodLineNumber"] = lineNumber;  // record line number of calling method
    }
    err = jvmtiEnv->Deallocate((unsigned char*)lineTable);
    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate lineTable");

    /* Get jclass object of calling method*/
    if ((err = jvmtiEnv->GetMethodDeclaringClass(method, &klass)) == JVMTI_ERROR_NONE) {
        /* Get source file name */
        err = jvmtiEnv->GetSourceFileName(klass, &fileName);
        if (err == JVMTI_ERROR_NONE) {
            jdata["callingMethodSourceFile"] = fileName;
        }
    }
    err = jvmtiEnv->Deallocate((unsigned char*)fileName);
    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate fileName");

    // Get information from stack
    if (backTraceEnabled) { // only run when backtrace is enabled
        if (numExceptions % exceptionSampleRate == 0) {
            jvmtiFrameInfo frames[EXCEPTION_STACK_TRACE_NUM_FRAMES];
            jint count;
            auto jMethods = json::array();

            err = jvmtiEnv->GetStackTrace(thread, 0, EXCEPTION_STACK_TRACE_NUM_FRAMES,
                                        frames, &count);
            if (err == JVMTI_ERROR_NONE && count >= 1) {
                json jMethod;
                for (int i = 0; i < count; i++) {
                    /* Get method name */
                    err = jvmtiEnv->GetMethodName(frames[i].method,
                                        &methodName, NULL, NULL);
                    if (err == JVMTI_ERROR_NONE) {
                        jMethod["methodName"] = methodName;
                    }
                    err = jvmtiEnv->Deallocate((unsigned char*)methodName);
                    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate methodName");

                    /* Get line number */
                    err = jvmtiEnv->GetLineNumberTable(frames[i].method, &lineCount, &lineTable);
                    if (err == JVMTI_ERROR_NONE) { // Find line
                        lineNumber = lineTable[0].line_number;
                        for (int j = 1; j < lineCount; j++) {
                            if (frames[i].location < lineTable[i].start_location) {
                                break;
                            } else {
                                lineNumber = lineTable[i].line_number;
                            }
                        }
                        jMethod["lineNumber"] = lineNumber;
                    }
                    err = jvmtiEnv->Deallocate((unsigned char*)lineTable);
                    check_jvmti_error(jvmtiEnv, err, "Unable to deallocate lineTable");

                    /* Get jclass object of calling method */
                    if ((err = jvmtiEnv->GetMethodDeclaringClass(frames[i].method, &klass))
                            == JVMTI_ERROR_NONE)
                    {
                        /* Get file name */
                        err = jvmtiEnv->GetSourceFileName(klass, &fileName);
                        if (err == JVMTI_ERROR_NONE) {
                            jMethod["fileName"] = fileName;
                        }
                        err = jvmtiEnv->Deallocate((unsigned char*)fileName);
                        check_jvmti_error(jvmtiEnv, err, "Unable to deallocate fileName");
                    }

                    /* Add frame's json element to backtrace json element array */
                    if (!jMethod.empty()) {
                        jMethods.push_back(jMethod);
                    }
                }
            }

            jdata["backtrace"] = jMethods;
        }
    }

    // err = jvmtiEnv->Deallocate((unsigned char*)fileName);
    // err = jvmtiEnv->Deallocate((unsigned char*)methodName);
    // err = jvmtiEnv->Deallocate((unsigned char*)lineTable);
    sendToServer(jdata.dump());
}
