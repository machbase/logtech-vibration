#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <machbase_sqlcli.h>

#define ERROR_CHECK_COUNT       100000

#define RC_SUCCESS              0
#define RC_FAILURE              -1

#define UNUSED(aVar) do { (void)(aVar); } while(0)

#define CHECK_APPEND_RESULT(aRC, aEnv, aCon, aSTMT)             \
    if( !SQL_SUCCEEDED(aRC) )                                   \
    {                                                           \
        if( checkAppendError(aEnv, aCon, aSTMT) == RC_FAILURE ) \
        {                                                       \
            ;                                                   \
        }                                                       \
    }

typedef struct tm timestruct;

SQLHENV     gEnv;
SQLHDBC     gCon;
SQLHDBC     gLotDataConn;

static char          gTargetIP[16] = "127.0.0.1";
static int           gPortNo=5656;
static unsigned long gMaxData=1000000000;
static long          gTps=2048;
static unsigned int  gEquipCnt = 1;
static unsigned int  gTagPerEq = 16;
static int           gDataPerSec = 2048;
int                  gNoLotNo=0;

time_t getTimeStamp();
void printError(SQLHENV aEnv, SQLHDBC aCon, SQLHSTMT aStmt, char *aMsg);
int connectDB();
void disconnectDB();
int executeDirectSQL(const char *aSQL, int aErrIgnore);
int createTable();
int appendOpen(char *aTableName, SQLHSTMT aStmt);
int appendData(SQLHSTMT aStmt);
void appendTps(SQLHSTMT aStmt);
unsigned long appendClose(SQLHSTMT aStmt);

time_t getTimeStamp()
{
    struct timeval sTimeVal;
    int            sRet;

    sRet = gettimeofday(&sTimeVal, NULL);

    if (sRet == 0)
    {
        return (time_t)(sTimeVal.tv_sec * 1000000 + sTimeVal.tv_usec);
    }
    else
    {
        return 0;
    }
}

void printError(SQLHENV aEnv, SQLHDBC aCon, SQLHSTMT aStmt, char *aMsg)
{
    SQLINTEGER      sNativeError;
    SQLCHAR         sErrorMsg[SQL_MAX_MESSAGE_LENGTH + 1];
    SQLCHAR         sSqlState[SQL_SQLSTATE_SIZE + 1];
    SQLSMALLINT     sMsgLength;

    if( aMsg != NULL )
    {
        printf("%s\n", aMsg);
    }

    if( SQLError(aEnv, aCon, aStmt, sSqlState, &sNativeError,
        sErrorMsg, SQL_MAX_MESSAGE_LENGTH, &sMsgLength) == SQL_SUCCESS )
    {
        printf("SQLSTATE-[%s], Machbase-[%d][%s]\n", sSqlState, sNativeError, sErrorMsg);
    }
}

int checkAppendError(SQLHENV aEnv, SQLHDBC aCon, SQLHSTMT aStmt)
{
    SQLINTEGER      sNativeError;
    SQLCHAR         sErrorMsg[SQL_MAX_MESSAGE_LENGTH + 1];
    SQLCHAR         sSqlState[SQL_SQLSTATE_SIZE + 1];
    SQLSMALLINT     sMsgLength;

    if( SQLError(aEnv, aCon, aStmt, sSqlState, &sNativeError,
        sErrorMsg, SQL_MAX_MESSAGE_LENGTH, &sMsgLength) != SQL_SUCCESS )
    {
        return RC_FAILURE;
    }

    printf("SQLSTATE-[%s], Machbase-[%d][%s]\n", sSqlState, sNativeError, sErrorMsg);

    if( sNativeError != 9604 &&
        sNativeError != 9605 &&
        sNativeError != 9606 )
    {
        return RC_FAILURE;
    }

    return RC_SUCCESS;
}

void appendDumpError(SQLHSTMT    aStmt,
                 SQLINTEGER  aErrorCode,
                 SQLPOINTER  aErrorMessage,
                 SQLLEN      aErrorBufLen,
                 SQLPOINTER  aRowBuf,
                 SQLLEN      aRowBufLen)
{
    char       sErrMsg[1024] = {0, };
    char       sRowMsg[32 * 1024] = {0, };

    UNUSED(aStmt);

    if (aErrorMessage != NULL)
    {
        strncpy(sErrMsg, (char *)aErrorMessage, aErrorBufLen);
    }

    if (aRowBuf != NULL)
    {
        strncpy(sRowMsg, (char *)aRowBuf, aRowBufLen);
    }

    fprintf(stdout, "Append Error : [%d][%s]\n[%s]\n\n", aErrorCode, sErrMsg, sRowMsg);
}

int connectDB()
{
    char sConnStr[1024];

    if( SQLAllocEnv(&gEnv) != SQL_SUCCESS )
    {
        printf("SQLAllocEnv error\n");
        return RC_FAILURE;
    }

    if( SQLAllocConnect(gEnv, &gCon) != SQL_SUCCESS )
    {
        printf("SQLAllocConnect error\n");

        SQLFreeEnv(gEnv);
        gEnv = SQL_NULL_HENV;

        return RC_FAILURE;
    }

    sprintf(sConnStr,"SERVER=%s;UID=SYS;PWD=MANAGER;CONNTYPE=1;PORT_NO=%d", gTargetIP, gPortNo);

    if( SQLDriverConnect( gCon, NULL,
                          (SQLCHAR *)sConnStr,
                          SQL_NTS,
                          NULL, 0, NULL,
                          SQL_DRIVER_NOPROMPT ) != SQL_SUCCESS
      )
    {

        printError(gEnv, gCon, NULL, "SQLDriverConnect error");

        SQLFreeConnect(gCon);
        gCon = SQL_NULL_HDBC;

        SQLFreeEnv(gEnv);
        gEnv = SQL_NULL_HENV;

        return RC_FAILURE;
    }

    return RC_SUCCESS;
}

void disconnectDB()
{
    if( SQLDisconnect(gCon) != SQL_SUCCESS )
    {
        printError(gEnv, gCon, NULL, "SQLDisconnect error");
    }

    SQLFreeConnect(gCon);
    gCon = SQL_NULL_HDBC;

    SQLFreeEnv(gEnv);
    gEnv = SQL_NULL_HENV;
}

int executeDirectSQL(const char *aSQL, int aErrIgnore)
{
    SQLHSTMT sStmt = SQL_NULL_HSTMT;

    if( SQLAllocStmt(gCon, &sStmt) != SQL_SUCCESS )
    {
        if( aErrIgnore == 0 )
        {
            printError(gEnv, gCon, sStmt, "SQLAllocStmt Error");
            return RC_FAILURE;
        }
    }

    if( SQLExecDirect(sStmt, (SQLCHAR *)aSQL, SQL_NTS) != SQL_SUCCESS )
    {

        if( aErrIgnore == 0 )
        {
            printError(gEnv, gCon, sStmt, "SQLExecDirect Error");

            SQLFreeStmt(sStmt,SQL_DROP);
            sStmt = SQL_NULL_HSTMT;
            return RC_FAILURE;
        }
    }

    if( SQLFreeStmt(sStmt, SQL_DROP) != SQL_SUCCESS )
    {
        if (aErrIgnore == 0)
        {
            printError(gEnv, gCon, sStmt, "SQLFreeStmt Error");
            sStmt = SQL_NULL_HSTMT;
            return RC_FAILURE;
        }
    }

    sStmt = SQL_NULL_HSTMT;
    return RC_SUCCESS;
}

int createTable()
{
    int sRC;

    sRC = executeDirectSQL("DROP TABLE TAG", 1);
    if( sRC != RC_SUCCESS )
    {
        sRC = 0;
    }

    sRC = executeDirectSQL("CREATE TAG TABLE TAG(NAME VARCHAR(32) PRIMARY KEY, TIME DATETIME BASETIME, VALUE DOUBLE SUMMARIZED)", 0);
    if( sRC != RC_SUCCESS )
    {
        return RC_FAILURE;
    }

    return RC_SUCCESS;
}

int appendOpen(char *aTableName, SQLHSTMT aStmt)
{
    if( SQLAppendOpen(aStmt, (SQLCHAR *)aTableName, ERROR_CHECK_COUNT) != SQL_SUCCESS )
    {
        printError(gEnv, gCon, aStmt, "SQLAppendOpen Error");
        return RC_FAILURE;
    }

    return RC_SUCCESS;
}

int appendData(SQLHSTMT aStmt)
{
    SQL_APPEND_PARAM *sParam;
    SQLRETURN        sRC;
    unsigned long    i;
    unsigned int     j,p;
    unsigned long    sRecCount = 0;
    char             sTagName[20];
    int              sTag;
    double           sValue;
    time_t current_time;
    struct tm *local_time_info;
    int               year,month,hour,min,sec,day;

    struct tm         sTm;
    unsigned long     sTime;
    int               sInterval;
    long              StartTime;
    int               sBreak = 0;

    year     =    2019;
    month    =    0;
    day      =    1;
    hour     =    0;
    min      =    0;
    sec      =    0;

    memset(&sTm, 0, sizeof(struct tm));
    /* sTm.tm_year = year - 1900; */
    /* sTm.tm_mon  = month; */
    /* sTm.tm_mday = day; */
    /* sTm.tm_hour = hour; */
    /* sTm.tm_min  = min; */
    /* sTm.tm_sec  = sec; */

    sTime = time(NULL);//mktime(&sTm);
    sTime = sTime * MACHBASE_UINT64_LITERAL(1000000000);

    if (gNoLotNo == 0)
    {
        sParam = malloc(sizeof(SQL_APPEND_PARAM) * 4);
        memset(sParam, 0, sizeof(SQL_APPEND_PARAM) *4);
    }
    else
    {
        sParam = malloc(sizeof(SQL_APPEND_PARAM) * 3);
        memset(sParam, 0, sizeof(SQL_APPEND_PARAM)*3);
    }
    sInterval = (int)(1000000000/gDataPerSec); // 100ms default.

    StartTime = getTimeStamp();
    while(1)
    {
        current_time = time(NULL);
    
        // UTC 초를 지역 시간 정보로 변환
        local_time_info = localtime(&current_time);

        fprintf(stderr, "Time=%s", asctime(local_time_info));

        sTime = time(NULL);//mktime(&sTm);
        sTime = sTime * MACHBASE_UINT64_LITERAL(1000000000);

        for( i = 0; i < gTps; i++ )
        {
            unsigned int sEq = 0;

            for( j=0; j< (gEquipCnt * gTagPerEq); j++)
            {
                /* tag_id */
                sTag = j;
                sTagName[0] = 0;
                snprintf(sTagName, 20, "EQ%d^TAG%d",sEq, sTag);
                sParam[0].mVar.mLength   = strnlen(sTagName,20);
                sParam[0].mVar.mData     = sTagName;
                sParam[1].mDateTime.mTime =  sTime;
                sParam[2].mDouble = (rand()%20000)/100.0; //0 ~ 200

                sRC = SQLAppendDataV2(aStmt, sParam);
                CHECK_APPEND_RESULT(sRC, gEnv, gCon, aStmt);
                if (sTag % gTagPerEq == 0 && sTag != 0)
                {
                    sEq ++;
                    if (sEq == gEquipCnt) sEq = 0;
                }
            }
            sTime = sTime + sInterval;
        }
        sleep(1);
    }
exit:

    return RC_SUCCESS;
}

unsigned long appendClose(SQLHSTMT aStmt)
{
    SQLBIGINT sSuccessCount = 0;
    SQLBIGINT sFailureCount = 0;

    if( SQLAppendClose(aStmt, (SQLBIGINT *)&sSuccessCount, (SQLBIGINT *)&sFailureCount) != SQL_SUCCESS )
    {
        printError(gEnv, gCon, aStmt, "SQLAppendClose Error");
        return RC_FAILURE;
    }
    else
    {
        printf("success : %ld, failure : %ld\n", sSuccessCount, sFailureCount);

        return sSuccessCount;
    }
}


int main(int argc, char **argv)
{
    SQLHSTMT    sStmt = SQL_NULL_HSTMT;

    unsigned long  sCount=0;
    time_t         sStartTime, sEndTime;

    char      *sTableName;
    if (argc != 2)
    {
        printf("sample_append tablename\n");
        exit(0);
    }

    sTableName = argv[1];

    if( connectDB() == RC_SUCCESS )
    {
        printf("connectDB success\n");
    }
    else
    {
        printf("connectDB failure\n");
        goto error;
    }

    if( createTable() == RC_SUCCESS )
    {
        printf("createTable success\n");
    }
    else
    {
        printf("createTable failure\n");
        goto error;
    }


    if( SQLAllocStmt(gCon, &sStmt) != SQL_SUCCESS )
    {
        printError(gEnv, gCon, sStmt, "SQLAllocStmt Error");
        goto error;
    }

    if( appendOpen(sTableName, sStmt) == RC_SUCCESS )
    {
        printf("appendOpen success\n");
    }
    else
    {
        printf("appendOpen failure\n");
        goto error;
    }

    if( SQLAppendSetErrorCallback(sStmt, appendDumpError) != SQL_SUCCESS )
    {
        printError(gEnv, gCon, sStmt, "SQLAppendSetErrorCallback Error");
        goto error;
    }

    sStartTime = getTimeStamp();
    if( appendData(sStmt) != SQL_SUCCESS )
    {
        printf("append failure");
        goto error;
    }
    sEndTime = getTimeStamp();

    sCount = appendClose(sStmt);
    if( sCount > 0 )
    {
        printf("appendClose success\n");
        printf("timegap = %ld microseconds for %ld records\n", sEndTime - sStartTime, sCount);
        printf("%.2f records/second\n",  ((double)sCount/(double)(sEndTime - sStartTime))*1000000);
    }
    else
    {
        printf("appendClose failure\n");
    }

    if( SQLFreeStmt(sStmt, SQL_DROP) != SQL_SUCCESS )
    {
        printError(gEnv, gCon, sStmt, "SQLFreeStmt Error");
        goto error;
    }
    sStmt = SQL_NULL_HSTMT;

    disconnectDB();

    return RC_SUCCESS;

error:
    if( sStmt != SQL_NULL_HSTMT )
    {
        SQLFreeStmt(sStmt, SQL_DROP);
        sStmt = SQL_NULL_HSTMT;
    }

    if( gCon != SQL_NULL_HDBC )
    {
        disconnectDB();
    }

    return RC_FAILURE;
}
