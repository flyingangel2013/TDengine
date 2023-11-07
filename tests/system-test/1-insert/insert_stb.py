import sys 
from util.log import *
from util.cases import *
from util.sql import *
from util.dnodes import tdDnodes
from math import inf
import taos

class TDTestCase:
    def caseDescription(self):
        '''
        case1<shenglian zhou>: [TS-3932] insert into stb 
        ''' 
        return
    
    def init(self, conn, logSql, replicaVer=1):
        tdLog.debug("start to execute %s" % __file__)
        tdSql.init(conn.cursor(), True)
        self.conn = conn
        
        
    def restartTaosd(self, index=1, dbname="db"):
        tdDnodes.stop(index)
        tdDnodes.startWithoutSleep(index)
        tdSql.execute(f"use insert_stb")


    def run_normal(self):
        print("running {}".format(__file__))
        tdSql.execute("drop database if exists insert_stb")
        tdSql.execute("create database if not exists insert_stb")
        tdSql.execute('use insert_stb')
        tdSql.execute('create database d1')

        tdSql.execute('create database d2')

        tdSql.execute('use d1;')

        tdSql.execute('create table st(ts timestamp, f int) tags(t int);')

        tdSql.execute("insert into ct1 using st tags(1) values('2021-04-19 00:00:00', 1);")

        tdSql.execute("insert into ct2 using st tags(2) values('2021-04-19 00:00:01', 2);")

        tdSql.execute("insert into ct1 values('2021-04-19 00:00:02', 2);")

        tdSql.execute('use d2;')

        tdSql.execute('create table st(ts timestamp, f int) tags(t int);')

        tdSql.execute("insert into ct1 using st tags(1) values('2021-04-19 00:00:00', 1);")

        tdSql.execute("insert into ct2 using st tags(2) values('2021-04-19 00:00:01', 2);")

        tdSql.execute('create database db1 vgroups 1;')

        tdSql.execute('create table db1.stb (ts timestamp, c1 int, c2 int) tags(t1 int, t2 int);')

        tdSql.execute('use d1;')

        tdSql.execute("insert into st (tbname, ts, f, t) values('ct3', '2021-04-19 08:00:03', 3, 3);")

        tdSql.execute("insert into d1.st (tbname, ts, f) values('ct6', '2021-04-19 08:00:04', 6);")

        tdSql.execute("insert into d1.st (tbname, ts, f) values('ct6', '2021-04-19 08:00:05', 7)('ct8', '2021-04-19 08:00:06', 8);")

        tdSql.execute("insert into d1.st (tbname, ts, f, t) values('ct6', '2021-04-19 08:00:07', 9, 9)('ct8', '2021-04-19 08:00:08', 10, 10);")

        tdSql.execute("insert into d1.st (tbname, ts, f, t) values('ct6', '2021-04-19 08:00:09', 9, 9)('ct8', '2021-04-19 08:00:10', 10, 10) d2.st (tbname, ts, f, t) values('ct6', '2021-04-19 08:00:11', 9, 9)('ct8', '2021-04-19 08:00:12', 10, 10);")

        tdSql.query('select * from d1.st order by ts;')
        tdSql.checkRows(11)
        tdSql.checkData(0, 0, datetime.datetime(2021, 4, 19, 0, 0))
        tdSql.checkData(0, 1, 1)
        tdSql.checkData(0, 2, 1)
        tdSql.checkData(1, 0, datetime.datetime(2021, 4, 19, 0, 0, 1))
        tdSql.checkData(1, 1, 2)
        tdSql.checkData(1, 2, 2)
        tdSql.checkData(2, 0, datetime.datetime(2021, 4, 19, 0, 0, 2))
        tdSql.checkData(2, 1, 2)
        tdSql.checkData(2, 2, 1)
        tdSql.checkData(3, 0, datetime.datetime(2021, 4, 19, 8, 0, 3))
        tdSql.checkData(3, 1, 3)
        tdSql.checkData(3, 2, 3)
        tdSql.checkData(4, 0, datetime.datetime(2021, 4, 19, 8, 0, 4))
        tdSql.checkData(4, 1, 6)
        tdSql.checkData(4, 2, None)
        tdSql.checkData(5, 0, datetime.datetime(2021, 4, 19, 8, 0, 5))
        tdSql.checkData(5, 1, 7)
        tdSql.checkData(5, 2, None)
        tdSql.checkData(6, 0, datetime.datetime(2021, 4, 19, 8, 0, 6))
        tdSql.checkData(6, 1, 8)
        tdSql.checkData(6, 2, None)
        tdSql.checkData(7, 0, datetime.datetime(2021, 4, 19, 8, 0, 7))
        tdSql.checkData(7, 1, 9)
        tdSql.checkData(7, 2, None)
        tdSql.checkData(8, 0, datetime.datetime(2021, 4, 19, 8, 0, 8))
        tdSql.checkData(8, 1, 10)
        tdSql.checkData(8, 2, None)
        tdSql.checkData(9, 0, datetime.datetime(2021, 4, 19, 8, 0, 9))
        tdSql.checkData(9, 1, 9)
        tdSql.checkData(9, 2, None)
        tdSql.checkData(10, 0, datetime.datetime(2021, 4, 19, 8, 0, 10))
        tdSql.checkData(10, 1, 10)
        tdSql.checkData(10, 2, None)

        tdSql.query('select * from d2.st order by ts;')
        tdSql.checkRows(4)
        tdSql.checkData(0, 0, datetime.datetime(2021, 4, 19, 0, 0))
        tdSql.checkData(0, 1, 1)
        tdSql.checkData(0, 2, 1)
        tdSql.checkData(1, 0, datetime.datetime(2021, 4, 19, 0, 0, 1))
        tdSql.checkData(1, 1, 2)
        tdSql.checkData(1, 2, 2)
        tdSql.checkData(2, 0, datetime.datetime(2021, 4, 19, 8, 0, 11))
        tdSql.checkData(2, 1, 9)
        tdSql.checkData(2, 2, 9)
        tdSql.checkData(3, 0, datetime.datetime(2021, 4, 19, 8, 0, 12))
        tdSql.checkData(3, 1, 10)
        tdSql.checkData(3, 2, 10)

        tdSql.execute("insert into d2.st(ts, f, tbname) values('2021-04-19 08:00:13', 1, 'ct1') d1.ct1 values('2021-04-19 08:00:14', 1);")

        tdSql.query('select * from d1.st order by ts;')
        tdSql.checkRows(12)
        tdSql.checkData(0, 0, datetime.datetime(2021, 4, 19, 0, 0))
        tdSql.checkData(0, 1, 1)
        tdSql.checkData(0, 2, 1)
        tdSql.checkData(1, 0, datetime.datetime(2021, 4, 19, 0, 0, 1))
        tdSql.checkData(1, 1, 2)
        tdSql.checkData(1, 2, 2)
        tdSql.checkData(2, 0, datetime.datetime(2021, 4, 19, 0, 0, 2))
        tdSql.checkData(2, 1, 2)
        tdSql.checkData(2, 2, 1)
        tdSql.checkData(3, 0, datetime.datetime(2021, 4, 19, 8, 0, 3))
        tdSql.checkData(3, 1, 3)
        tdSql.checkData(3, 2, 3)
        tdSql.checkData(4, 0, datetime.datetime(2021, 4, 19, 8, 0, 4))
        tdSql.checkData(4, 1, 6)
        tdSql.checkData(4, 2, None)
        tdSql.checkData(5, 0, datetime.datetime(2021, 4, 19, 8, 0, 5))
        tdSql.checkData(5, 1, 7)
        tdSql.checkData(5, 2, None)
        tdSql.checkData(6, 0, datetime.datetime(2021, 4, 19, 8, 0, 6))
        tdSql.checkData(6, 1, 8)
        tdSql.checkData(6, 2, None)
        tdSql.checkData(7, 0, datetime.datetime(2021, 4, 19, 8, 0, 7))
        tdSql.checkData(7, 1, 9)
        tdSql.checkData(7, 2, None)
        tdSql.checkData(8, 0, datetime.datetime(2021, 4, 19, 8, 0, 8))
        tdSql.checkData(8, 1, 10)
        tdSql.checkData(8, 2, None)
        tdSql.checkData(9, 0, datetime.datetime(2021, 4, 19, 8, 0, 9))
        tdSql.checkData(9, 1, 9)
        tdSql.checkData(9, 2, None)
        tdSql.checkData(10, 0, datetime.datetime(2021, 4, 19, 8, 0, 10))
        tdSql.checkData(10, 1, 10)
        tdSql.checkData(10, 2, None)
        tdSql.checkData(11, 0, datetime.datetime(2021, 4, 19, 8, 0, 14))
        tdSql.checkData(11, 1, 1)
        tdSql.checkData(11, 2, 1)

        tdSql.query('select * from d2.st order by ts;')
        tdSql.checkRows(5)
        tdSql.checkData(0, 0, datetime.datetime(2021, 4, 19, 0, 0))
        tdSql.checkData(0, 1, 1)
        tdSql.checkData(0, 2, 1)
        tdSql.checkData(1, 0, datetime.datetime(2021, 4, 19, 0, 0, 1))
        tdSql.checkData(1, 1, 2)
        tdSql.checkData(1, 2, 2)
        tdSql.checkData(2, 0, datetime.datetime(2021, 4, 19, 8, 0, 11))
        tdSql.checkData(2, 1, 9)
        tdSql.checkData(2, 2, 9)
        tdSql.checkData(3, 0, datetime.datetime(2021, 4, 19, 8, 0, 12))
        tdSql.checkData(3, 1, 10)
        tdSql.checkData(3, 2, 10)
        tdSql.checkData(4, 0, datetime.datetime(2021, 4, 19, 8, 0, 13))
        tdSql.checkData(4, 1, 1)
        tdSql.checkData(4, 2, 1)
    
    def run_insert_stb(self):
        print("running {}".format('insert_stb'))
        self.conn.select_db('insert_stb')
        tdSql.execute('create table stb1 (ts timestamp, c1 bool, c2 tinyint, c3 smallint, c4 int, c5 bigint, c6 float, c7 double, c8 binary(10), c9 nchar(10), c10 tinyint unsigned, c11 smallint unsigned, c12 int unsigned, c13 bigint unsigned) TAGS(t1 int, t2 binary(10), t3 double);')

        tdSql.execute('insert into stb1(ts,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,t1,t2,t3,tbname) values (\'2021-11-11 09:00:00\',true,1,1,1,1,1,1,"123","1234",1,1,1,1, 1, \'1\', 1.0, \'tb1\');')

        tdSql.execute("insert into stb1(ts,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,t1,t2,t3,tbname) values ('2021-11-11 09:00:01',true,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL, 2, '2', 2.0, 'tb1');")

        tdSql.execute('insert into stb1(ts,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,t1,t2,t3,tbname) values (\'2021-11-11 09:00:02\',true,2,NULL,2,NULL,2,NULL,"234",NULL,2,NULL,2,NULL, 2, \'2\', 2.0, \'tb2\');')

        tdSql.execute('insert into stb1(ts,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,t1,t2,t3,tbname) values (\'2021-11-11 09:00:03\',false,NULL,3,NULL,3,NULL,3,NULL,"3456",NULL,3,NULL,3, 3, \'3\', 3.0, \'tb3\');')

        tdSql.execute('insert into stb1(ts,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,t1,t2,t3,tbname) values (\'2021-11-11 09:00:04\',true,4,4,4,4,4,4,"456","4567",4,4,4,4, 4, \'4.0\', 4.0, \'tb4\');')

        tdSql.execute('insert into stb1(ts,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,t1,t2,t3,tbname) values (\'2021-11-11 09:00:05\',true,127,32767,2147483647,9223372036854775807,3.402823466e+38,1.79769e+308,"567","5678",254,65534,4294967294,9223372036854775807, 5, \'5\', 5, \'max\' );')

        tdSql.execute('insert into stb1(ts,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,t1,t2,t3,tbname) values (\'2021-11-11 09:00:06\',true,-127,-32767,-2147483647,-9223372036854775807,-3.402823466e+38,-1.79769e+308,"678","6789",0,0,0,0, 6, \'6\', 6, \'min\');')

        tdSql.execute('insert into stb1(ts,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,tbname,t1,t2,t3) values (\'2021-11-11 09:00:07\',true,-127,-32767,-2147483647,-9223372036854775807,-3.402823466e+38,-1.79769e+308,"678","6789",0,0,0,0, \'min\', 6, \'6\', 6);')

        tdSql.query('select tbname,* from stb1 order by ts;')
        tdSql.checkRows(8)
        tdSql.checkData(0, 0, 'tb1')
        tdSql.checkData(0, 1, datetime.datetime(2021, 11, 11, 9, 0))
        tdSql.checkData(0, 2, True)
        tdSql.checkData(0, 3, 1)
        tdSql.checkData(0, 4, 1)
        tdSql.checkData(0, 5, 1)
        tdSql.checkData(0, 6, 1)
        tdSql.checkData(0, 7, 1.0)
        tdSql.checkData(0, 8, 1.0)
        tdSql.checkData(0, 9, '123')
        tdSql.checkData(0, 10, '1234')
        tdSql.checkData(0, 11, 1)
        tdSql.checkData(0, 12, 1)
        tdSql.checkData(0, 13, 1)
        tdSql.checkData(0, 14, 1)
        tdSql.checkData(0, 15, 1)
        tdSql.checkData(0, 16, '1')
        tdSql.checkData(0, 17, 1.0)
        tdSql.checkData(1, 0, 'tb1')
        tdSql.checkData(1, 1, datetime.datetime(2021, 11, 11, 9, 0, 1))
        tdSql.checkData(1, 2, True)
        tdSql.checkData(1, 3, None)
        tdSql.checkData(1, 4, None)
        tdSql.checkData(1, 5, None)
        tdSql.checkData(1, 6, None)
        tdSql.checkData(1, 7, None)
        tdSql.checkData(1, 8, None)
        tdSql.checkData(1, 9, None)
        tdSql.checkData(1, 10, None)
        tdSql.checkData(1, 11, None)
        tdSql.checkData(1, 12, None)
        tdSql.checkData(1, 13, None)
        tdSql.checkData(1, 14, None)
        tdSql.checkData(1, 15, 1)
        tdSql.checkData(1, 16, '1')
        tdSql.checkData(1, 17, 1.0)
        tdSql.checkData(2, 0, 'tb2')
        tdSql.checkData(2, 1, datetime.datetime(2021, 11, 11, 9, 0, 2))
        tdSql.checkData(2, 2, True)
        tdSql.checkData(2, 3, 2)
        tdSql.checkData(2, 4, None)
        tdSql.checkData(2, 5, 2)
        tdSql.checkData(2, 6, None)
        tdSql.checkData(2, 7, 2.0)
        tdSql.checkData(2, 8, None)
        tdSql.checkData(2, 9, '234')
        tdSql.checkData(2, 10, None)
        tdSql.checkData(2, 11, 2)
        tdSql.checkData(2, 12, None)
        tdSql.checkData(2, 13, 2)
        tdSql.checkData(2, 14, None)
        tdSql.checkData(2, 15, 2)
        tdSql.checkData(2, 16, '2')
        tdSql.checkData(2, 17, 2.0)
        tdSql.checkData(3, 0, 'tb3')
        tdSql.checkData(3, 1, datetime.datetime(2021, 11, 11, 9, 0, 3))
        tdSql.checkData(3, 2, False)
        tdSql.checkData(3, 3, None)
        tdSql.checkData(3, 4, 3)
        tdSql.checkData(3, 5, None)
        tdSql.checkData(3, 6, 3)
        tdSql.checkData(3, 7, None)
        tdSql.checkData(3, 8, 3.0)
        tdSql.checkData(3, 9, None)
        tdSql.checkData(3, 10, '3456')
        tdSql.checkData(3, 11, None)
        tdSql.checkData(3, 12, 3)
        tdSql.checkData(3, 13, None)
        tdSql.checkData(3, 14, 3)
        tdSql.checkData(3, 15, 3)
        tdSql.checkData(3, 16, '3')
        tdSql.checkData(3, 17, 3.0)
        tdSql.checkData(4, 0, 'tb4')
        tdSql.checkData(4, 1, datetime.datetime(2021, 11, 11, 9, 0, 4))
        tdSql.checkData(4, 2, True)
        tdSql.checkData(4, 3, 4)
        tdSql.checkData(4, 4, 4)
        tdSql.checkData(4, 5, 4)
        tdSql.checkData(4, 6, 4)
        tdSql.checkData(4, 7, 4.0)
        tdSql.checkData(4, 8, 4.0)
        tdSql.checkData(4, 9, '456')
        tdSql.checkData(4, 10, '4567')
        tdSql.checkData(4, 11, 4)
        tdSql.checkData(4, 12, 4)
        tdSql.checkData(4, 13, 4)
        tdSql.checkData(4, 14, 4)
        tdSql.checkData(4, 15, 4)
        tdSql.checkData(4, 16, '4.0')
        tdSql.checkData(4, 17, 4.0)
        tdSql.checkData(5, 0, 'max')
        tdSql.checkData(5, 1, datetime.datetime(2021, 11, 11, 9, 0, 5))
        tdSql.checkData(5, 2, True)
        tdSql.checkData(5, 3, 127)
        tdSql.checkData(5, 4, 32767)
        tdSql.checkData(5, 5, 2147483647)
        tdSql.checkData(5, 6, 9223372036854775807)
        tdSql.checkData(5, 7, 3.4028234663852886e+38)
        tdSql.checkData(5, 8, 1.79769e+308)
        tdSql.checkData(5, 9, '567')
        tdSql.checkData(5, 10, '5678')
        tdSql.checkData(5, 11, 254)
        tdSql.checkData(5, 12, 65534)
        tdSql.checkData(5, 13, 4294967294)
        tdSql.checkData(5, 14, 9223372036854775807)
        tdSql.checkData(5, 15, 5)
        tdSql.checkData(5, 16, '5')
        tdSql.checkData(5, 17, 5.0)
        tdSql.checkData(6, 0, 'min')
        tdSql.checkData(6, 1, datetime.datetime(2021, 11, 11, 9, 0, 6))
        tdSql.checkData(6, 2, True)
        tdSql.checkData(6, 3, -127)
        tdSql.checkData(6, 4, -32767)
        tdSql.checkData(6, 5, -2147483647)
        tdSql.checkData(6, 6, -9223372036854775807)
        tdSql.checkData(6, 7, -3.4028234663852886e+38)
        tdSql.checkData(6, 8, -1.79769e+308)
        tdSql.checkData(6, 9, '678')
        tdSql.checkData(6, 10, '6789')
        tdSql.checkData(6, 11, 0)
        tdSql.checkData(6, 12, 0)
        tdSql.checkData(6, 13, 0)
        tdSql.checkData(6, 14, 0)
        tdSql.checkData(6, 15, 6)
        tdSql.checkData(6, 16, '6')
        tdSql.checkData(6, 17, 6.0)
        tdSql.checkData(7, 0, 'min')
        tdSql.checkData(7, 1, datetime.datetime(2021, 11, 11, 9, 0, 7))
        tdSql.checkData(7, 2, True)
        tdSql.checkData(7, 3, -127)
        tdSql.checkData(7, 4, -32767)
        tdSql.checkData(7, 5, -2147483647)
        tdSql.checkData(7, 6, -9223372036854775807)
        tdSql.checkData(7, 7, -3.4028234663852886e+38)
        tdSql.checkData(7, 8, -1.79769e+308)
        tdSql.checkData(7, 9, '678')
        tdSql.checkData(7, 10, '6789')
        tdSql.checkData(7, 11, 0)
        tdSql.checkData(7, 12, 0)
        tdSql.checkData(7, 13, 0)
        tdSql.checkData(7, 14, 0)
        tdSql.checkData(7, 15, 6)
        tdSql.checkData(7, 16, '6')
        tdSql.checkData(7, 17, 6.0)

    def run_stmt_error(self):
        conn = self.conn
        conn.select_db('insert_stb')
        conn.execute('create table stb9(ts timestamp, f int) tags (t int)')
        try:
            stmt = conn.statement("insert into stb9(tbname, f, t) values('ctb91', 1, ?)")
            params = taos.new_bind_params(1)
            params[0].int(1)
            stmt.bind_param(params)
            stmt.execute()
            result = stmt.use_result()
        except Exception as err:
            print(str(err))    
        
    def run_consecutive_seq(self):
        print("running {}".format("consecutive_seq"))
        tdSql.execute("drop database if exists insert_stb3")
        tdSql.execute("create database if not exists insert_stb3")
        tdSql.execute('use insert_stb3')
        tdSql.execute('create table st (ts timestamp, ti tinyint, si smallint, i int, bi bigint, f float, d double, b binary(10)) tags(t1 int, t2 float, t3 binary(10))')

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct0', 0, 0.000000, 'childtable', 1546300800000, 0, 0, 0, 0, 0.000000, 0.000000, 'hello') ('ct0', 0, 0.000000, 'childtable', 1546300800001, 1, 1, 1, 1, 1.000000, 2.000000, 'hello')")

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct1', 1, 1.000000, 'childtable', 1546301800000, 64, 16960, 1000000, 1000000, 1000000.000000, 2000000.000000, 'hello') ('ct1', 1, 1.000000, 'childtable', 1546301800001, 65, 16961, 1000001, 1000001, 1000001.000000, 2000002.000000, 'hello')")

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct2', 2, 2.000000, 'childtable', 1546302800000, -128, -31616, 2000000, 2000000, 2000000.000000, 4000000.000000, 'hello') ('ct2', 2, 2.000000, 'childtable', 1546302800001, -127, -31615, 2000001, 2000001, 2000001.000000, 4000002.000000, 'hello')")

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct3', 3, 3.000000, 'childtable', 1546303800000, -64, -14656, 3000000, 3000000, 3000000.000000, 6000000.000000, 'hello') ('ct3', 3, 3.000000, 'childtable', 1546303800001, -63, -14655, 3000001, 3000001, 3000001.000000, 6000002.000000, 'hello')")

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct4', 4, 4.000000, 'childtable', 1546304800000, 0, 2304, 4000000, 4000000, 4000000.000000, 8000000.000000, 'hello') ('ct4', 4, 4.000000, 'childtable', 1546304800001, 1, 2305, 4000001, 4000001, 4000001.000000, 8000002.000000, 'hello')")

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct5', 5, 5.000000, 'childtable', 1546305800000, 64, 19264, 5000000, 5000000, 5000000.000000, 10000000.000000, 'hello') ('ct5', 5, 5.000000, 'childtable', 1546305800001, 65, 19265, 5000001, 5000001, 5000001.000000, 10000002.000000, 'hello')")

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct6', 6, 6.000000, 'childtable', 1546306800000, -128, -29312, 6000000, 6000000, 6000000.000000, 12000000.000000, 'hello') ('ct6', 6, 6.000000, 'childtable', 1546306800001, -127, -29311, 6000001, 6000001, 6000001.000000, 12000002.000000, 'hello')")

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct7', 7, 7.000000, 'childtable', 1546307800000, -64, -12352, 7000000, 7000000, 7000000.000000, 14000000.000000, 'hello') ('ct7', 7, 7.000000, 'childtable', 1546307800001, -63, -12351, 7000001, 7000001, 7000001.000000, 14000002.000000, 'hello')")

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct8', 8, 8.000000, 'childtable', 1546308800000, 0, 4608, 8000000, 8000000, 8000000.000000, 16000000.000000, 'hello') ('ct8', 8, 8.000000, 'childtable', 1546308800001, 1, 4609, 8000001, 8000001, 8000001.000000, 16000002.000000, 'hello')")

        tdSql.execute("insert into st(tbname, t1, t2, t3, ts, ti, si, i, bi, f, d, b) values ('ct9', 9, 9.000000, 'childtable', 1546309800000, 64, 21568, 9000000, 9000000, 9000000.000000, 18000000.000000, 'hello') ('ct9', 9, 9.000000, 'childtable', 1546309800001, 65, 21569, 9000001, 9000001, 9000001.000000, 18000002.000000, 'hello')")

        tdSql.query('select * from st order by ts')
        tdSql.checkRows(20)
        tdSql.checkData(0, 0, datetime.datetime(2019, 1, 1, 8, 0))
        tdSql.checkData(0, 1, 0)
        tdSql.checkData(0, 2, 0)
        tdSql.checkData(0, 3, 0)
        tdSql.checkData(0, 4, 0)
        tdSql.checkData(0, 5, 0.0)
        tdSql.checkData(0, 6, 0.0)
        tdSql.checkData(0, 7, 'hello')
        tdSql.checkData(0, 8, 0)
        tdSql.checkData(0, 9, 0.0)
        tdSql.checkData(0, 10, 'childtable')
        tdSql.checkData(1, 0, datetime.datetime(2019, 1, 1, 8, 0, 0, 1000))
        tdSql.checkData(1, 1, 1)
        tdSql.checkData(1, 2, 1)
        tdSql.checkData(1, 3, 1)
        tdSql.checkData(1, 4, 1)
        tdSql.checkData(1, 5, 1.0)
        tdSql.checkData(1, 6, 2.0)
        tdSql.checkData(1, 7, 'hello')
        tdSql.checkData(1, 8, 0)
        tdSql.checkData(1, 9, 0.0)
        tdSql.checkData(1, 10, 'childtable')
        tdSql.checkData(2, 0, datetime.datetime(2019, 1, 1, 8, 16, 40))
        tdSql.checkData(2, 1, 64)
        tdSql.checkData(2, 2, 16960)
        tdSql.checkData(2, 3, 1000000)
        tdSql.checkData(2, 4, 1000000)
        tdSql.checkData(2, 5, 1000000.0)
        tdSql.checkData(2, 6, 2000000.0)
        tdSql.checkData(2, 7, 'hello')
        tdSql.checkData(2, 8, 1)
        tdSql.checkData(2, 9, 1.0)
        tdSql.checkData(2, 10, 'childtable')
        tdSql.checkData(3, 0, datetime.datetime(2019, 1, 1, 8, 16, 40, 1000))
        tdSql.checkData(3, 1, 65)
        tdSql.checkData(3, 2, 16961)
        tdSql.checkData(3, 3, 1000001)
        tdSql.checkData(3, 4, 1000001)
        tdSql.checkData(3, 5, 1000001.0)
        tdSql.checkData(3, 6, 2000002.0)
        tdSql.checkData(3, 7, 'hello')
        tdSql.checkData(3, 8, 1)
        tdSql.checkData(3, 9, 1.0)
        tdSql.checkData(3, 10, 'childtable')
        tdSql.checkData(4, 0, datetime.datetime(2019, 1, 1, 8, 33, 20))
        tdSql.checkData(4, 1, -128)
        tdSql.checkData(4, 2, -31616)
        tdSql.checkData(4, 3, 2000000)
        tdSql.checkData(4, 4, 2000000)
        tdSql.checkData(4, 5, 2000000.0)
        tdSql.checkData(4, 6, 4000000.0)
        tdSql.checkData(4, 7, 'hello')
        tdSql.checkData(4, 8, 2)
        tdSql.checkData(4, 9, 2.0)
        tdSql.checkData(4, 10, 'childtable')
        tdSql.checkData(5, 0, datetime.datetime(2019, 1, 1, 8, 33, 20, 1000))
        tdSql.checkData(5, 1, -127)
        tdSql.checkData(5, 2, -31615)
        tdSql.checkData(5, 3, 2000001)
        tdSql.checkData(5, 4, 2000001)
        tdSql.checkData(5, 5, 2000001.0)
        tdSql.checkData(5, 6, 4000002.0)
        tdSql.checkData(5, 7, 'hello')
        tdSql.checkData(5, 8, 2)
        tdSql.checkData(5, 9, 2.0)
        tdSql.checkData(5, 10, 'childtable')
        tdSql.checkData(6, 0, datetime.datetime(2019, 1, 1, 8, 50))
        tdSql.checkData(6, 1, -64)
        tdSql.checkData(6, 2, -14656)
        tdSql.checkData(6, 3, 3000000)
        tdSql.checkData(6, 4, 3000000)
        tdSql.checkData(6, 5, 3000000.0)
        tdSql.checkData(6, 6, 6000000.0)
        tdSql.checkData(6, 7, 'hello')
        tdSql.checkData(6, 8, 3)
        tdSql.checkData(6, 9, 3.0)
        tdSql.checkData(6, 10, 'childtable')
        tdSql.checkData(7, 0, datetime.datetime(2019, 1, 1, 8, 50, 0, 1000))
        tdSql.checkData(7, 1, -63)
        tdSql.checkData(7, 2, -14655)
        tdSql.checkData(7, 3, 3000001)
        tdSql.checkData(7, 4, 3000001)
        tdSql.checkData(7, 5, 3000001.0)
        tdSql.checkData(7, 6, 6000002.0)
        tdSql.checkData(7, 7, 'hello')
        tdSql.checkData(7, 8, 3)
        tdSql.checkData(7, 9, 3.0)
        tdSql.checkData(7, 10, 'childtable')
        tdSql.checkData(8, 0, datetime.datetime(2019, 1, 1, 9, 6, 40))
        tdSql.checkData(8, 1, 0)
        tdSql.checkData(8, 2, 2304)
        tdSql.checkData(8, 3, 4000000)
        tdSql.checkData(8, 4, 4000000)
        tdSql.checkData(8, 5, 4000000.0)
        tdSql.checkData(8, 6, 8000000.0)
        tdSql.checkData(8, 7, 'hello')
        tdSql.checkData(8, 8, 4)
        tdSql.checkData(8, 9, 4.0)
        tdSql.checkData(8, 10, 'childtable')
        tdSql.checkData(9, 0, datetime.datetime(2019, 1, 1, 9, 6, 40, 1000))
        tdSql.checkData(9, 1, 1)
        tdSql.checkData(9, 2, 2305)
        tdSql.checkData(9, 3, 4000001)
        tdSql.checkData(9, 4, 4000001)
        tdSql.checkData(9, 5, 4000001.0)
        tdSql.checkData(9, 6, 8000002.0)
        tdSql.checkData(9, 7, 'hello')
        tdSql.checkData(9, 8, 4)
        tdSql.checkData(9, 9, 4.0)
        tdSql.checkData(9, 10, 'childtable')
        tdSql.checkData(10, 0, datetime.datetime(2019, 1, 1, 9, 23, 20))
        tdSql.checkData(10, 1, 64)
        tdSql.checkData(10, 2, 19264)
        tdSql.checkData(10, 3, 5000000)
        tdSql.checkData(10, 4, 5000000)
        tdSql.checkData(10, 5, 5000000.0)
        tdSql.checkData(10, 6, 10000000.0)
        tdSql.checkData(10, 7, 'hello')
        tdSql.checkData(10, 8, 5)
        tdSql.checkData(10, 9, 5.0)
        tdSql.checkData(10, 10, 'childtable')
        tdSql.checkData(11, 0, datetime.datetime(2019, 1, 1, 9, 23, 20, 1000))
        tdSql.checkData(11, 1, 65)
        tdSql.checkData(11, 2, 19265)
        tdSql.checkData(11, 3, 5000001)
        tdSql.checkData(11, 4, 5000001)
        tdSql.checkData(11, 5, 5000001.0)
        tdSql.checkData(11, 6, 10000002.0)
        tdSql.checkData(11, 7, 'hello')
        tdSql.checkData(11, 8, 5)
        tdSql.checkData(11, 9, 5.0)
        tdSql.checkData(11, 10, 'childtable')
        tdSql.checkData(12, 0, datetime.datetime(2019, 1, 1, 9, 40))
        tdSql.checkData(12, 1, -128)
        tdSql.checkData(12, 2, -29312)
        tdSql.checkData(12, 3, 6000000)
        tdSql.checkData(12, 4, 6000000)
        tdSql.checkData(12, 5, 6000000.0)
        tdSql.checkData(12, 6, 12000000.0)
        tdSql.checkData(12, 7, 'hello')
        tdSql.checkData(12, 8, 6)
        tdSql.checkData(12, 9, 6.0)
        tdSql.checkData(12, 10, 'childtable')
        tdSql.checkData(13, 0, datetime.datetime(2019, 1, 1, 9, 40, 0, 1000))
        tdSql.checkData(13, 1, -127)
        tdSql.checkData(13, 2, -29311)
        tdSql.checkData(13, 3, 6000001)
        tdSql.checkData(13, 4, 6000001)
        tdSql.checkData(13, 5, 6000001.0)
        tdSql.checkData(13, 6, 12000002.0)
        tdSql.checkData(13, 7, 'hello')
        tdSql.checkData(13, 8, 6)
        tdSql.checkData(13, 9, 6.0)
        tdSql.checkData(13, 10, 'childtable')
        tdSql.checkData(14, 0, datetime.datetime(2019, 1, 1, 9, 56, 40))
        tdSql.checkData(14, 1, -64)
        tdSql.checkData(14, 2, -12352)
        tdSql.checkData(14, 3, 7000000)
        tdSql.checkData(14, 4, 7000000)
        tdSql.checkData(14, 5, 7000000.0)
        tdSql.checkData(14, 6, 14000000.0)
        tdSql.checkData(14, 7, 'hello')
        tdSql.checkData(14, 8, 7)
        tdSql.checkData(14, 9, 7.0)
        tdSql.checkData(14, 10, 'childtable')
        tdSql.checkData(15, 0, datetime.datetime(2019, 1, 1, 9, 56, 40, 1000))
        tdSql.checkData(15, 1, -63)
        tdSql.checkData(15, 2, -12351)
        tdSql.checkData(15, 3, 7000001)
        tdSql.checkData(15, 4, 7000001)
        tdSql.checkData(15, 5, 7000001.0)
        tdSql.checkData(15, 6, 14000002.0)
        tdSql.checkData(15, 7, 'hello')
        tdSql.checkData(15, 8, 7)
        tdSql.checkData(15, 9, 7.0)
        tdSql.checkData(15, 10, 'childtable')
        tdSql.checkData(16, 0, datetime.datetime(2019, 1, 1, 10, 13, 20))
        tdSql.checkData(16, 1, 0)
        tdSql.checkData(16, 2, 4608)
        tdSql.checkData(16, 3, 8000000)
        tdSql.checkData(16, 4, 8000000)
        tdSql.checkData(16, 5, 8000000.0)
        tdSql.checkData(16, 6, 16000000.0)
        tdSql.checkData(16, 7, 'hello')
        tdSql.checkData(16, 8, 8)
        tdSql.checkData(16, 9, 8.0)
        tdSql.checkData(16, 10, 'childtable')
        tdSql.checkData(17, 0, datetime.datetime(2019, 1, 1, 10, 13, 20, 1000))
        tdSql.checkData(17, 1, 1)
        tdSql.checkData(17, 2, 4609)
        tdSql.checkData(17, 3, 8000001)
        tdSql.checkData(17, 4, 8000001)
        tdSql.checkData(17, 5, 8000001.0)
        tdSql.checkData(17, 6, 16000002.0)
        tdSql.checkData(17, 7, 'hello')
        tdSql.checkData(17, 8, 8)
        tdSql.checkData(17, 9, 8.0)
        tdSql.checkData(17, 10, 'childtable')
        tdSql.checkData(18, 0, datetime.datetime(2019, 1, 1, 10, 30))
        tdSql.checkData(18, 1, 64)
        tdSql.checkData(18, 2, 21568)
        tdSql.checkData(18, 3, 9000000)
        tdSql.checkData(18, 4, 9000000)
        tdSql.checkData(18, 5, 9000000.0)
        tdSql.checkData(18, 6, 18000000.0)
        tdSql.checkData(18, 7, 'hello')
        tdSql.checkData(18, 8, 9)
        tdSql.checkData(18, 9, 9.0)
        tdSql.checkData(18, 10, 'childtable')
        tdSql.checkData(19, 0, datetime.datetime(2019, 1, 1, 10, 30, 0, 1000))
        tdSql.checkData(19, 1, 65)
        tdSql.checkData(19, 2, 21569)
        tdSql.checkData(19, 3, 9000001)
        tdSql.checkData(19, 4, 9000001)
        tdSql.checkData(19, 5, 9000001.0)
        tdSql.checkData(19, 6, 18000002.0)
        tdSql.checkData(19, 7, 'hello')
        tdSql.checkData(19, 8, 9)
        tdSql.checkData(19, 9, 9.0)
        tdSql.checkData(19, 10, 'childtable')

        tdSql.execute('drop database insert_stb3')
        
    def run(self):
        self.run_normal()
        self.run_insert_stb()
        self.run_stmt_error()
        self.run_consecutive_seq()
        
        
    def stop(self):
        tdSql.close()
        tdLog.success("%s successfully executed" % __file__)

tdCases.addWindows(__file__, TDTestCase())
tdCases.addLinux(__file__, TDTestCase())
