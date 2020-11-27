#include <jvmti.h>
#include <string.h>
#include "agentOptions.hpp"
#include "methodEntry.hpp"
#include "json.hpp"
#include "server.hpp"
#include "infra.hpp"

#include <iostream>
#include <atomic>

using json = nlohmann::json;

std::atomic<int> mEntrySampleCount {0};
std::atomic<int> mEntrySampleRate {1};

// set sample rate according to command instructions
// requirement: rate > 0
void setMethodEntrySampleRate(int rate) {
    mEntrySampleRate = rate;
}

/*** retrieves method name and line number, and declaring class name and signature
 *      for every nth method entry                                                 ***/
JNIEXPORT void JNICALL MethodEntry(jvmtiEnv *jvmtiEnv,
            JNIEnv* env,
            jthread thread,
            jmethodID method) {

    if (mEntrySampleCount % mEntrySampleRate == 0) {           

        json j;
        jvmtiError err;
        char *name_ptr;
        char *signature_ptr;
        char *generic_ptr;
        char *declaringClassName;
        jclass declaring_class;
        jint entry_count_ptr;
        jvmtiLineNumberEntry* table_ptr;
        jlocation start_loc_ptr;
        jlocation end_loc_ptr;

        j["methodNum"] = mEntrySampleCount.load();
        
        err = jvmtiEnv->GetMethodName(method, &name_ptr, &signature_ptr, &generic_ptr);
        if (err == JVMTI_ERROR_NONE) {
            j["methodName"] = name_ptr;
            j["methodSig"] = signature_ptr;
            err = jvmtiEnv->GetMethodDeclaringClass(method, &declaring_class);
            if (err == JVMTI_ERROR_NONE) {
                err = jvmtiEnv->GetClassSignature(declaring_class, &declaringClassName, NULL);
                if (err == JVMTI_ERROR_NONE) {
                    j["methodClass"] = declaringClassName;
                    err = jvmtiEnv->GetMethodLocation(method, &start_loc_ptr, &end_loc_ptr);
                    if (err == JVMTI_ERROR_NONE) {
                        err = jvmtiEnv->GetLineNumberTable(method, &entry_count_ptr, &table_ptr);
                        if (err == JVMTI_ERROR_NONE) {
                            j["methodLineNum"] = table_ptr->line_number;
                        } else {
                            printf("(GetLineNumberTable) Error received: %d\n", err);
                        }
                    } else {
                        printf("(GetMethodLocation) Error received: %d\n", err);
                    }
                } else {
                    printf("(GetClassSignature) Error received: %d\n", err);
                }
            } else {
                printf("(GetMethodDeclaringClass) Error received: %d\n", err);
            }
        } else {
            printf("(GetMethodName) Error received: %d\n", err);
        }               

        err = jvmtiEnv->Deallocate((unsigned char*)name_ptr);
        if (err != JVMTI_ERROR_NONE) {
            printf("(Deallocate name_ptr) Error received: %d\n", err);
        }
        err = jvmtiEnv->Deallocate((unsigned char*)signature_ptr);
        if (err != JVMTI_ERROR_NONE) {
            printf("(Deallocate signature_ptr) Error received: %d\n", err);
        }
        err = jvmtiEnv->Deallocate((unsigned char*)generic_ptr);
        if (err != JVMTI_ERROR_NONE) {
            printf("(Deallocate generic_ptr) Error received: %d\n", err);
        }
        err = jvmtiEnv->Deallocate((unsigned char*)declaringClassName);
        if (err != JVMTI_ERROR_NONE) {
            printf("(Deallocate declaringClassName) Error received: %d\n", err);
        }
        err = jvmtiEnv->Deallocate((unsigned char*)table_ptr);
        if (err != JVMTI_ERROR_NONE) {
            printf("(Deallocate table_ptr) Error received: %d\n", err);
        }
    
        std::string s = j.dump();
        // printf("\n%s\n", s.c_str());
        // printf("sample num: %i", mEntrySampleCount);
        sendToServer(s);
    }
    mEntrySampleCount++;
}
