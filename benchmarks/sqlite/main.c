#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#include "coz.h"

#define THREAD_COUNT 64
#define INSERTIONS_PER_THREAD 10000

void* thread_fn(void* arg) {
  int id = (int)(intptr_t)arg;
  
  int rc;
  sqlite3* db;
  char* err;

  if(sqlite3_open(":memory:", &db)) {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }
  
  sqlite3_exec(db, "PRAGMA synchronous=OFF", NULL, NULL, &err);
  sqlite3_exec(db, "PRAGMA journal_mode=MEMORY", NULL, NULL, &err);
  
  char buf[512];
  sprintf(buf, "DROP TABLE IF EXISTS tab%d", id);
  
  do {
    rc = sqlite3_exec(db, buf, NULL, NULL, &err);
  } while(rc == SQLITE_BUSY);
  if(rc) {
    printf("Error: %s\n", err);
    abort();
  }
  
  sprintf(buf, "CREATE TABLE tab%d(id INTEGER PRIMARY KEY, x INTEGER, y INTEGER, z TEXT)", id);
  do {
    rc = sqlite3_exec(db, buf, NULL, NULL, &err);
  } while(rc == SQLITE_BUSY);
  if(rc) {
    printf("Error: %s\n", err);
    abort();
  }
  
  sprintf(buf, "INSERT INTO tab%d VALUES(?1, ?2, ?3, ?4)", id);
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(db, buf, strlen(buf), &stmt, NULL);
 
  for (int i=0; i<INSERTIONS_PER_THREAD; i++) {
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &err);
  
    sqlite3_bind_int(stmt, 1, i + id*INSERTIONS_PER_THREAD);
    sqlite3_bind_int(stmt, 2, 2*i);
    sqlite3_bind_int(stmt, 3, 3*i);
    sqlite3_bind_text(stmt, 4, "asdf", 4, SQLITE_STATIC);

    do {
      rc = sqlite3_step(stmt);
    } while(rc == SQLITE_BUSY);
    
    if(rc != SQLITE_DONE) {
      printf("Commit Failed!\n%s\n", sqlite3_errmsg(db));
      abort();
    }
    
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, &err);
    
    COZ_PROGRESS;

    sqlite3_reset(stmt);
  }
  
  sqlite3_finalize(stmt);
  
  sprintf(buf, "DROP TABLE tab%d", id);
  sqlite3_exec(db, buf, NULL, NULL, &err);
  
  sqlite3_close(db);
  
  return NULL;
}

int main(int argc, char **argv){
  if(sqlite3_threadsafe() != 2) {
    printf("SQLite is not thread safe. What happened?\n");
    return 1;
  }
  
  sqlite3_enable_shared_cache(0);
 
  pthread_t threads[THREAD_COUNT];
  for(uintptr_t i=0; i<THREAD_COUNT; i++) {
    pthread_create(&threads[i], NULL, thread_fn, (void*)i);
  }
  
  for(int i=0; i<THREAD_COUNT; i++) {
    pthread_join(threads[i], NULL);
  }
  
  return 0;
}
