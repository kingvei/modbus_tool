#ifndef APPFLOW_H
#define APPFLOW_H

#include "common.h"
#include "json.h"
#include "modbus.h"

u16 freq_2_tick(float f); //频率转换成间隔
float tick_2_freq(u16 tick); //间隔转换成频率

class CMReg //modbus寄存器定义
{
public:
	CMReg(){}
	string name=""; //寄存器名称
	u8 addr=1; //从机地址
	u16 reg=0; //寄存器地址
	u8 is_curv=0; //是否加入曲线显示
	double d_k=1; //数据放大系数
	double d_off=0; //数据偏移
	Json::Value toJson(void)
	{
		Json::Value v;
		v["name"]=name;
		v["addr"]=addr;
		v["reg"]=reg;
		v["is_curv"]=is_curv;
		v["d_k"]=d_k;
		v["d_off"]=d_off;
		return v;
	}
	int fromJson(Json::Value &v)
	{
		if(v["name"].isString() && 
			v["reg"].isInt() && 
			v["is_curv"].isInt() && 
			v["addr"].isInt())
		{
			name=v["name"].asString();
			reg=v["reg"].asInt();
			is_curv=v["is_curv"].asInt();
			addr=v["addr"].asInt();
			jsonget(v,"d_k",d_k);
			jsonget(v,"d_off",d_off);
			return 0;
		}
		return 1;
	}
	u16 dbuf=0; //寄存器的内存空间
	u16 val_2_org(float f) //值转换为原始u16
	{
		return (f-d_off)/d_k;
	}
	float org_2_val(u16 d) //原始u16转换成值
	{
		return d*d_k+d_off;
	}
	int need_update_UI=0; //是否需要刷新显示
};
class CMTask //modbus周期任务
{
public:
	CMTask()
	{
		memset(&mdbs_buf,0,sizeof(mdbs_buf));
		mdbs_buf.buf=(u16*)task_buf;
	}
	CMTask(const CMTask &t)
	{
		name=t.name;
		enable=t.enable;
		freq=t.freq;
		tick=t.tick;
		mdbs_buf=t.mdbs_buf;
		mdbs_buf.buf=(u16*)task_buf;
		need_update_UI=t.need_update_UI;
	}
	string name=""; //寄存器名称
	int enable=0; //是否有效
	float freq=1; //任务频率
	u32 tick=0; //计数
	MODBUS_ADDR_LIST mdbs_buf; //modbus任务对象
	u8 task_buf[256]; //任务缓存
	Json::Value toJson(void)
	{
		Json::Value v;
		v["name"]=name;
		v["addr"]=mdbs_buf.addr;
		v["reg"]=mdbs_buf.st;
		v["type"]=mdbs_buf.type;
		v["num"]=mdbs_buf.num;
		v["fre"]=freq;
		return v;
	}
	int fromJson(Json::Value &v)
	{
		if(v["name"].isString() && 
			v["reg"].isInt() && 
			v["type"].isInt() && 
			v["num"].isInt() && 
			v["fre"].isDouble() && 
			v["addr"].isInt())
		{
			name=v["name"].asString();
			mdbs_buf.st=v["reg"].asInt();
			mdbs_buf.type=v["type"].asInt();
			mdbs_buf.num=v["num"].asInt();
			freq=v["fre"].asDouble();
			mdbs_buf.addr=v["addr"].asInt();
			mdbs_buf.err=0;
			mdbs_buf.stat=0;
			enable=1;
			return 0;
		}
		return 1;
	}
	int need_update_UI=0; //是否需要刷新显示
};
//需外部提供
extern void update_a_reg(u8 addr,u16 reg,u16 d); //更新一个寄存器
extern void modbus_rxpack(u8 *p,int n); //modbus模块接收完整帧
//自身提供
extern vector<CMReg> regs_list; //寄存器列表
extern vector<CMTask> task_list; //任务列表
extern CModbus_Master main_md;
extern CModbus_Slave slave_md;
extern int is_master; //是否是主模式
extern int is_running; //是否正在运行

int app_ini(Json::Value v); //将配置文件读入内存列表中
void task_start(void); //开始任务,将任务列表中的任务变成modbus模块的任务
void task_stop(void); //结束任务
void task_poll(void); //任务周期函数，100Hz

#endif

