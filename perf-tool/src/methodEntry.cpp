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
        char *declaringClassName;
        jclass declaring_class;
        jint entry_count_ptr;
        jvmtiLineNumberEntry* table_ptr;
        // jlocation start_loc_ptr;
        // jlocation end_loc_ptr;

        j["methodNum"] = mEntrySampleCount.load();
        
        err = jvmtiEnv->GetMethodName(method, &name_ptr, &signature_ptr, NULL);
        if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Name.\n")) {
            j["methodName"] = name_ptr;
            j["methodSig"] = signature_ptr;
        }
        err = jvmtiEnv->GetMethodDeclaringClass(method, &declaring_class);
        if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Declaring Class.\n")) {
            err = jvmtiEnv->GetClassSignature(declaring_class, &declaringClassName, NULL);
            if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Declaring Class Signature.\n")) {
                j["methodClass"] = declaringClassName;
            }
        }
        // err = jvmtiEnv->GetMethodLocation(method, &start_loc_ptr, &end_loc_ptr);
        // if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Location.\n")) {
        //     j["start_loc"] = start_loc_ptr;
        // }
        err = jvmtiEnv->GetLineNumberTable(method, &entry_count_ptr, &table_ptr);
        if (check_jvmti_error(jvmtiEnv, err, "Unable to retrieve Method Line Number Table.\n")) {
            j["methodLineNum"] = table_ptr->line_number;
        }

        err = jvmtiEnv->Deallocate((unsigned char*)name_ptr);
        check_jvmti_error(jvmtiEnv, err, "Unable to deallocate name_ptr.\n");
        err = jvmtiEnv->Deallocate((unsigned char*)signature_ptr);
        check_jvmti_error(jvmtiEnv, err, "Unable to deallocate signature_ptr.\n");
        err = jvmtiEnv->Deallocate((unsigned char*)declaringClassName);
        check_jvmti_error(jvmtiEnv, err, "Unable to deallocate declaringClassName.\n");
        err = jvmtiEnv->Deallocate((unsigned char*)table_ptr);
        check_jvmti_error(jvmtiEnv, err, "Unable to deallocate table_ptr.\n");
    
        std::string s = j.dump();
        // printf("\n%s\n", s.c_str());
        // printf("sample num: %i", mEntrySampleCount);
        sendToServer(s);
    }
    mEntrySampleCount++;
}
