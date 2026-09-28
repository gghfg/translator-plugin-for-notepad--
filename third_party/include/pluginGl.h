#pragma once

// notepad-- (ndd) 插件 SDK 头文件。
// 与上游 src/include/pluginGl.h 保持一致，只补全了中文注释（上游文件注释为 GBK 编码，此处改为 UTF-8）。
//
// 重要：不要改动结构体字段的顺序和类型。NDD_PROC_DATA 是主程序与插件之间的 ABI 约定，
// 增删字段会导致插件无法被加载或行为异常。

#include <QString>
#include <QMenu>

#define NDD_EXPORTDLL

#if defined(Q_OS_WIN)
	#if defined(NDD_EXPORTDLL)
		#define NDD_EXPORT __declspec(dllexport)
	#else
		#define NDD_EXPORT __declspec(dllimport)
	#endif
#else
	#define NDD_EXPORT __attribute__((visibility("default")))
#endif

struct ndd_proc_data
{
	QString m_strPlugName; //插件名称。必选，主程序用它做菜单显示名
	QString m_strFilePath; //当前插件 lib 的完整路径。可选，由主程序在加载时回填
	QString m_strComment;  //菜单说明。可选
	QString m_version;     //版本号。可选
	QString m_auther;      //作者名。可选
	int m_menuType;        //菜单类型。0=使用主程序创建的单个菜单项 1=插件自建二级菜单
	QMenu* m_rootMenu;     //当 m_menuType = 1 时，主程序传入该插件的菜单根节点

	ndd_proc_data() : m_rootMenu(nullptr), m_menuType(0)
	{
	}
};

typedef struct ndd_proc_data NDD_PROC_DATA;

typedef bool (*NDD_PROC_IDENTIFY_CALLBACK)(NDD_PROC_DATA* pProcData);
typedef void (*NDD_PROC_FOUND_CALLBACK)(NDD_PROC_DATA* pProcData, void* pUserData);
