/*	IME接口函数
 *
 *	IME状态：
 *	1. START				没有输入
 *	2. EDIT					正在进行输入
 *	3. SELECT				按下“下箭头”后状态
 *	4. ENGLISH				英文状态
 *	5. SEND					上屏状态
 *	6. VInput				V input mode
 */

#include <config.h>
#include <utility.h>
#include <pim_state.h>
#include <editor.h>
#include <win32/pim_ime.h>
#include <win32/main_window.h>
#include <win32/softkbd.h>
#include <tchar.h>
#include <share_segment.h>

int last_key = 0;				//上一次的按键

TCHAR ui_window_class_name[] = UI_WINDOW_CLASS_NAME;

//保存最近输入的字词 by yuanzh 2008.5.7

//#pragma data_seg(HYPIM_SHARED_SEGMENT)
//static TCHAR szRecentResult[MAXRECENT][MAX_WORD_LENGTH + 1] = {0};
//static int nCurRecent = 0;  //数组中最后一项的下标
//#pragma data_seg()

/**	将context中的内容复制到IMC中去。
 */
void MakeCompositionAndCandidate(LPINPUTCONTEXT pIMC, PIMCONTEXT *context)
{
	int size, i;
	LPCOMPOSITIONSTRING pCompose;
	LPCANDIDATEINFO		pCandInfo;
	LPCANDIDATELIST		pCandList;

	pCompose = (LPCOMPOSITIONSTRING)ImmLockIMCC(pIMC->hCompStr);
	if (!pCompose)
		return;

	size = ImmGetIMCCSize(pIMC->hCompStr);
	if (!context->compose_length || size < context->compose_length)
	{
		pCompose->dwCompStrLen = 0;
		pCompose->dwCursorPos  = 0;
		ImmUnlockIMCC(pIMC->hCompStr);
		return;
	}

	pCompose->dwCompStrLen = context->compose_length;
	pCompose->dwCursorPos  = context->compose_cursor_index;

	memcpy((char*) pCompose +  sizeof(COMPOSITIONSTRING), context->compose_string, (context->compose_length + 1) * sizeof(TCHAR));

	ImmUnlockIMCC(pIMC->hCompStr);

	size = sizeof(CANDIDATEINFO) +
		   sizeof(CANDIDATELIST) +
		   sizeof(TCHAR*) * MAX_CANDIDATES_PER_LINE * MAX_CANDIDATE_LINES +					//指针长度
		   MAX_CANDIDATES_PER_LINE * MAX_CANDIDATE_LINES * MAX_CANDIDATE_STRING_LENGTH +	//候选串长度
		   0x2000;

	if (size > (int)ImmGetIMCCSize(pIMC->hCandInfo))
	{
		pCandInfo = (LPCANDIDATEINFO)ImmLockIMCC(pIMC->hCandInfo);
		pCandInfo->dwCount = 0;
		ImmUnlockIMCC(pIMC->hCandInfo);
		return;
	}

	pCandInfo = (LPCANDIDATEINFO)ImmLockIMCC(pIMC->hCandInfo);
	pCandList = (LPCANDIDATELIST)((char*)pCandInfo + pCandInfo->dwOffset[0]);
	if (!pCandList)
	{
		ImmUnlockIMCC(pIMC->hCandInfo);
		return;
	}

	pCandList->dwCount		= context->candidate_page_count;
	pCandList->dwSelection	= context->candidate_selected_index;
	pCandList->dwPageSize	= context->candidate_page_count;
	pCandList->dwPageStart	= 0;

	for (i = 0; i < context->candidate_page_count; i++)
	{
		_tcscpy((TCHAR *)((char *)pCandList + pCandList->dwOffset[i]),
			    context->candidate_string[i]);
	}

	//问题：DOS下abc风格，输入拼音的同时，候选显示上次输入的第一候选
	//因此需要清除上次的第一候选
	if (host_is_console && (pim_config->input_style == STYLE_ABC) && (!context->candidate_page_count))
		_tcscpy((TCHAR *)((char *)pCandList + pCandList->dwOffset[0]), TEXT("\0"));

	ImmUnlockIMCC(pIMC->hCandInfo);
}

/**	清除系统上下文中的结果数据
 */
void ClearIMCResult(LPINPUTCONTEXT pIMC)
{
	LPCOMPOSITIONSTRING compose_string;
	LPCANDIDATEINFO     candidate_info;
	LPCANDIDATELIST		candidate_list;

	if (!pIMC)
		return;

	if ((compose_string = (LPCOMPOSITIONSTRING)ImmLockIMCC(pIMC->hCompStr)) != 0)
	{
		compose_string->dwResultStrLen	= 0;
		compose_string->dwCompStrLen	= 0;
		compose_string->dwCursorPos		= 0;

		ImmUnlockIMCC(pIMC->hCompStr);
	}

	if ((candidate_info = (LPCANDIDATEINFO)ImmLockIMCC(pIMC->hCandInfo)) != 0)
	{
		candidate_list = (LPCANDIDATELIST)((char*)candidate_info + sizeof(CANDIDATEINFO));

		if (candidate_list)
			candidate_list->dwCount = 0;

		ImmUnlockIMCC(pIMC->hCandInfo);
	}
}

BOOL WINAPI ImeInquire(LPIMEINFO lpImeInfo, LPTSTR lpszUIClass, DWORD dwSystemInfoFlags)
{
	Log(LOG_ID, L"Called");

	lpImeInfo->dwPrivateDataSize = 0;
	lpImeInfo->fdwProperty		 = IME_PROP_KBD_CHAR_FIRST | IME_PROP_SPECIAL_UI | IME_PROP_CANDLIST_START_FROM_1 | IME_PROP_UNICODE;
    lpImeInfo->fdwConversionCaps = IME_CMODE_SYMBOL | IME_CMODE_FULLSHAPE | IME_CMODE_SOFTKBD;
    lpImeInfo->fdwSentenceCaps   = IME_SMODE_NONE;
    lpImeInfo->fdwUICaps		 = UI_CAP_SOFTKBD | UI_CAP_2700;
	lpImeInfo->fdwSCSCaps		 = SCS_CAP_COMPSTR;
	lpImeInfo->fdwSelectCaps	 = 0;

    _tcscpy(lpszUIClass, ui_window_class_name);

	if (dwSystemInfoFlags & IME_SYSINFO_WINLOGON)	// for windows logon security checking
		window_logon = 1;
	else
		window_logon = 0;

	return 1;
}

DWORD WINAPI ImeConversionList(HIMC  hIMC, LPCTSTR  lpSrc, LPCANDIDATELIST  lpDst, DWORD  dwBufLen, UINT  uFlag)
{
	Log(LOG_ID, L"被调用");
	return 0;
}

//执行配置程序
BOOL WINAPI ImeConfigure(HKL  hKL, HWND  hWnd, DWORD  dwMode, LPVOID  lpData)
{
	Log(LOG_ID, L"被调用");

	if (dwMode == 1)		//IME_CONFIG_GENERAL
	{
		RunConfigNormal();
		return 1;
	}

	if (dwMode ==  2)		//IME_CONFIG_REGWORD
	{
		RunConfigWordlib();
		return 1;
	}

	return 0;
}

BOOL WINAPI ImeDestroy(UINT uReserved)
{
	Log(LOG_ID, L"被调用");
	return 1;
}

BOOL WINAPI ImeSetActiveContext(HIMC hIMC, BOOL fFlag)
{
	Log(LOG_ID, L"ImeSetActiveContext被调用, hIMC = %x, flag:%x", hIMC, fFlag);
	return 1;
}

PIMCONTEXT *LockContext(HIMC hIMC, LPINPUTCONTEXT *pIMC)
{
	PIMCONTEXT *context;
	int size;

	*pIMC = ImmLockIMC(hIMC);
	if (!*pIMC)
		return 0;

	//1.由于“高级文字服务”被设置，必须在这里进行设置Context
	//2.由于Win8下进行输入法切换时，hCandInfo可能被置0，必须在这里进行设置Context
	size = ImmGetIMCCSize((*pIMC)->hPrivate);
	if (size < sizeof(PIMCONTEXT) || !((*pIMC)->hPrivate) || !((*pIMC)->hCandInfo) || !((*pIMC)->hCompStr))					
		if (!SetIMCContext(*pIMC))
			return 0;

	context = (PIMCONTEXT*) ImmLockIMCC((*pIMC)->hPrivate);
	return context;
}

void UnlockContext(HIMC hIMC, LPINPUTCONTEXT pIMC)
{
	if (!hIMC || !pIMC)
		return;

	ImmUnlockIMCC(pIMC->hPrivate);
	ImmUnlockIMC(hIMC);
}

//确定是否处理这个按键
BOOL WINAPI ImeProcessKey(HIMC hIMC, UINT uVirKey, LPARAM lParam, CONST LPBYTE lpbKeyState)
{
	PIMCONTEXT *context;
	LPINPUTCONTEXT pIMC;
	int ret = 0;

	Log(LOG_ID, L"virtual_key = %x", uVirKey);

	context = LockContext(hIMC, &pIMC);
	if (!context)
		return 0;

	if (pIMC->fOpen)
		ret = IsNeedKey(context, uVirKey, lParam, lpbKeyState);

	Log(LOG_ID, L"ret = %x", ret);

	UnlockContext(hIMC, pIMC);

	if (!ret)		//不处理的按键需要记录
	{
		if (uVirKey != VK_SHIFT || !(lpbKeyState[VK_CONTROL] & 0x80) || (lpbKeyState[VK_SHIFT] & 0x80))
			last_key = uVirKey;
	}

	//记录上次数字键输入状态
	if (!ret &&
		(lpbKeyState[uVirKey] & 0x80 || !(lParam & 0xC0000000)) && context->state == STATE_START &&
		!(lpbKeyState[VK_SHIFT] & 0x80) && !(lpbKeyState[VK_CONTROL] & 0x80) &&
		((uVirKey >= '0' && uVirKey <= '9') || (uVirKey >= 0x60 && uVirKey <= 0x69)))
		context->last_digital = 1;

	return ret;
}

BOOL WINAPI NotifyIME(HIMC  hIMC, DWORD  dwAction, DWORD  dwIndex, DWORD  dwValue)
{
	LPINPUTCONTEXT pIMC = 0;
	PIMCONTEXT *context = 0;

	Log(LOG_ID, L"被调用, hIMC:%x, dwAction:%x, dwIndex:%x, dwValue:%x", hIMC, dwAction, dwIndex, dwValue);

	if (!(context = LockContext(hIMC, &pIMC)))
		return 1;

	switch(dwAction)
	{
	case NI_COMPOSITIONSTR:
		if (dwIndex != CPS_CANCEL && dwIndex != CPS_COMPLETE)
			break;

		if (context->input_length)
		{
			HideMainWindow(context, context->ui_context);
			ResetContext(context);
			ClearIMCResult(pIMC);
			GenerateImeMessage(hIMC, WM_IME_ENDCOMPOSITION, 0, 0);
		}

		break;

	//IE7问题：
	//失去焦点以后，再返回时输入时，没有任何的窗口显示，问题在于
	//没有发送任何的IME_NOTIFY消息，只是调用了ImeNotify(action:3, value:c)而已，
	//这种情况下，尝试进行窗口的显示工作（可能对其他程序存在影响，需要确认）
	//IE7在失去焦点的时候，会将IME的UI窗口Destroy，而回来的时候，却不进行
	//恢复，造成的这个问题，目前没有解决方法。
	case NI_CONTEXTUPDATED:
		//if (dwValue == 6)		//PhotoShop问题造成，这样改写没有把握。
		//{
		//	if (pIMC)
		//		pIMC->fOpen = 1;
		//}

		//目前的结果来看，不起作用。
		switch(dwValue)
		{
		case IMC_SETCOMPOSITIONWINDOW:
			break;
		}
		break;

	}

	UnlockContext(hIMC, pIMC);
	return 1;
}

/**	设置当前的转换方式
 */
void SetConversionStatus(HIMC hIMC, LPINPUTCONTEXT pIMC, PIMCONTEXT *context)
{
	int mode = 0;

	if (!context || !pIMC)
		return;

	if (context->input_mode & CHINESE_MODE)
		mode |= IME_CMODE_NATIVE;

	if (!(pim_config->hz_option & HZ_SYMBOL_HALFSHAPE))
		mode |= IME_CMODE_FULLSHAPE;

	if (pim_config->hz_option & HZ_SYMBOL_CHINESE)
		mode |= IME_CMODE_SYMBOL;

	if (context->soft_keyboard)
		mode |= IME_CMODE_SOFTKBD;

	ImmSetConversionStatus(hIMC, mode, 0);
}

/**	获得当前的转换方式
 */
void GetConversionStatus(HIMC hIMC, LPINPUTCONTEXT pIMC, PIMCONTEXT *context)
{
	int conversion_mode, sentence_mode;

	ImmGetConversionStatus(hIMC, &conversion_mode, &sentence_mode);

	if (conversion_mode & IME_CMODE_SYMBOL)
		pim_config->hz_option |= HZ_SYMBOL_CHINESE;
	else
		pim_config->hz_option &= ~HZ_SYMBOL_CHINESE;

	if (conversion_mode & IME_CMODE_FULLSHAPE)
		pim_config->hz_option &= ~HZ_SYMBOL_HALFSHAPE;
	else
		pim_config->hz_option |= HZ_SYMBOL_HALFSHAPE;

	if (conversion_mode & IME_CMODE_SOFTKBD)
	{
		SelectSoftKBD(context, hIMC, pim_config->soft_kbd_index);
		context->softkbd_index = pim_config->soft_kbd_index;

		if (context->state != STATE_SOFTKBD)
		{
			ResetContext(context);

			context->soft_keyboard = 1;
			context->state		   = STATE_SOFTKBD;

			HideMainWindow(context, context->ui_context);
			ShowSoftKBDWindow();
		}
	}
	else
	{
		if (context->state == STATE_SOFTKBD)
		{
			HideSoftKBDWindow();
			context->soft_keyboard = 0;
			DeSelectSoftKBD();
			context->state = STATE_START;
		}
	}

	ResetContext(context);

	Log(LOG_ID, L"设置输入法输入状态");

	//为了避免Vista在设置为默认输入法后，覆盖输入法的Reg配置，
	//将下面语句注释掉
//	SaveConfigInternal(pim_config);
}

/**	设置输入法的上下文
 */
int SetIMCContext(LPINPUTCONTEXT pIMC)
{
	HIMCC context_handle, compose_handle, candidate_handle;
	LPCOMPOSITIONSTRING compose_string;
	LPCANDIDATEINFO     candidate_info;
	LPCANDIDATELIST		candidate_list;
	PIMCONTEXT			*context;
	int size, i;

	pIMC->fOpen = 0;		//为了解决DOS的中文输入切换问题而添加

	//在hIMC中设置本上下文的PIMCONTEXT
	size = sizeof(PIMCONTEXT);

	//没有这个IMCC，则要创建一个
	if (!pIMC->hPrivate)				
		pIMC->hPrivate = ImmCreateIMCC(sizeof(PIMCONTEXT));

	if ((int)ImmGetIMCCSize(pIMC->hPrivate) < size)
	{
		//在V3切换到V6的时候，这个句柄为0，所以先DestroyIMCC
		context_handle = ImmReSizeIMCC(pIMC->hPrivate, sizeof(PIMCONTEXT));
		if (!context_handle)
		{
			ImmDestroyIMCC(pIMC->hPrivate);
			context_handle = ImmReSizeIMCC(pIMC->hPrivate, sizeof(PIMCONTEXT));
			if (!context_handle)
				return 0;
		}
		pIMC->hPrivate = context_handle;
	}

	context = (PIMCONTEXT*) ImmLockIMCC(pIMC->hPrivate);
	if (!context)
		return 0;

	FirstTimeResetContext(context);
	ImmUnlockIMCC(pIMC->hPrivate);

	//没有这个IMCC，则要创建一个
	if (!pIMC->hCompStr)				
		pIMC->hCompStr = ImmCreateIMCC(sizeof(COMPOSITIONSTRING));

	//设置CompStr的大小，用于存放compose_string以及result_string（长度多申请16个字节，用于错误缓冲）
	size = sizeof(COMPOSITIONSTRING) + MAX_COMPOSE_LENGTH + MAX_RESULT_LENGTH + 0x10;

	if ((int)ImmGetIMCCSize(pIMC->hCompStr) < size)
	{
		compose_handle = ImmReSizeIMCC(pIMC->hCompStr, size);
		if (!compose_handle)
			return 0;
		pIMC->hCompStr = compose_handle;
	}

	compose_string = (LPCOMPOSITIONSTRING) ImmLockIMCC(pIMC->hCompStr);
	if (!compose_string)
		return 0;

	compose_string->dwSize			  = size;
	compose_string->dwCompStrLen	  = 0;
	compose_string->dwCompStrOffset   = sizeof(COMPOSITIONSTRING);
	compose_string->dwResultStrLen	  = 0;
	compose_string->dwResultStrOffset = compose_string->dwCompStrOffset + 0x10 + MAX_COMPOSE_LENGTH;

	ImmUnlockIMCC(pIMC->hCompStr);

	//没有这个IMCC，则要创建一个(该问题见于WIN8下的微软拼音输入法，先用微软
	//拼音输入法输入几个字母，不选择任何结果，直接按ctrl+shift切换为华宇拼音
	//输入法，再输入时应用程序崩溃。造成崩溃的原因是hCandInfo被置0。经分析可
	//能是微软拼音输入法在切换为其他输入法之前，将hCandInfo释放掉(用ImmDestroyIMCC)
	//并赋值为0，而所有IME输入法共用同一个输入法上下文结构，因此切换到华宇拼音输
	//入法时不会重新分配输入法上下文，我们需要先判断一下句柄是否还有效。
	if (!pIMC->hCandInfo)
		pIMC->hCandInfo = ImmCreateIMCC(sizeof(CANDIDATEINFO));

	//设置hCandInfo的大小，存放CandidateInfo与候选串
	size = sizeof(CANDIDATEINFO) +
		   sizeof(CANDIDATELIST) +
		   sizeof(TCHAR*) * MAX_CANDIDATES_PER_LINE * MAX_CANDIDATE_LINES +	//指针长度
		   MAX_CANDIDATES_PER_LINE * MAX_CANDIDATE_LINES * MAX_CANDIDATE_STRING_LENGTH + //候选串长度
		   0x2000;

	if ((int)ImmGetIMCCSize(pIMC->hCandInfo) < size)
	{
		candidate_handle = ImmReSizeIMCC(pIMC->hCandInfo, size);
		if (!candidate_handle)
			return 0;

		pIMC->hCandInfo = candidate_handle;
	}

	candidate_info = (LPCANDIDATEINFO) ImmLockIMCC(pIMC->hCandInfo);
	if (!candidate_info)
		return 0;

	candidate_info->dwSize		= size;
	candidate_info->dwCount		= 1;		//暂时处理一行
	candidate_info->dwOffset[0] = sizeof(CANDIDATEINFO);	//从尾部开始

	candidate_list				= (LPCANDIDATELIST)((char*)candidate_info + candidate_info->dwOffset[0]);
	candidate_list->dwSize		= size - sizeof(CANDIDATEINFO);
	candidate_list->dwStyle		= IME_CAND_READ;
	candidate_list->dwCount		= 0;
	candidate_list->dwSelection = 0;
	candidate_list->dwPageSize	= MAX_CANDIDATES_PER_LINE;

	for (i = 0; i < MAX_CANDIDATES_PER_LINE * MAX_CANDIDATE_LINES; i++)
		candidate_list->dwOffset[i] =
			sizeof(CANDIDATELIST) + //跳过头
			sizeof(TCHAR*) * MAX_CANDIDATES_PER_LINE * MAX_CANDIDATE_LINES +	//指针长度
			i * MAX_CANDIDATE_STRING_LENGTH; //候选串长度

	pIMC->fOpen = 1;

	return 1;
}

/**	重要的问题
 *	当输入法设置为“将高级文字服务支持应用于所有程序时”
 *	系统可能不调用ImeSelect!!!
 *
 *	因此，我们必须将Context的申请放置在其他位置
 *	2007-7-31
 */
BOOL WINAPI ImeSelect(HIMC hIMC, BOOL selected)
{
	LPINPUTCONTEXT pIMC;
	PIMCONTEXT *context;

	//Vista IE7两个TAB中切出输入法时，会造成死锁，所以将下面的语句除掉
	//Lock();

	Log(LOG_ID, L"选中IME，hIMC=%x, select=%d", hIMC, selected);

	pIMC = ImmLockIMC(hIMC);
	if (!pIMC)
		return 1;

	Log(LOG_ID, L"选中IME，hIMC=%x, select=%d, fopen:%d", hIMC, selected, pIMC->fOpen);

	if (!selected)
	{
		pIMC->fOpen = 0;
		ImmUnlockIMC(hIMC);
		DeSelectSoftKBD();
		return 1;
	}

	//加载配置数据
	MaintainConfig();

	//设置Context
	if (SetIMCContext(pIMC))
	{
		//设置转换模式
		context = (PIMCONTEXT *)ImmLockIMCC(pIMC->hPrivate);
		SetConversionStatus(hIMC, pIMC, context);
		ImmUnlockIMCC(pIMC->hPrivate);
	}

	pIMC->fOpen = 1;

//	Unlock();

	return 1;
}

BOOL WINAPI ImeSetCompositionString(HIMC hIMC, DWORD dwIndex, LPCVOID lpComp, DWORD dwCompLen, LPCVOID lpRead, DWORD dwReadLen)
{
	Log(LOG_ID, L"被调用");
	return 0;
}

/**	通过剪贴板进行短语上屏
 */
void UploadLongString(LPINPUTCONTEXT pIMC, PIMCONTEXT *context)
{
	HGLOBAL h_mem;
	TCHAR *p_string;
	int length;

	length = (int)_tcslen(context->result_string);
	if (!length)
		return;

	//打开剪贴板
	if (!OpenClipboard(pIMC->hWnd))
		return;

	//清空剪贴板
	EmptyClipboard();

	//分配内存
	h_mem = GlobalAlloc(GMEM_MOVEABLE, (length + 1) * sizeof(TCHAR));
	if (!h_mem)
	{
		CloseClipboard();
		return;
	}

	p_string = GlobalLock(h_mem);
	_tcscpy_s(p_string, length + 1, context->result_string);
	GlobalUnlock(h_mem);

	SetClipboardData(CF_UNICODETEXT, h_mem);
	CloseClipboard();

	//由于Word等Office程序不支持WM_PASTE消息的处理，因此，采用键盘事件方式处理
	//发送CTRL-V键盘消息
	{
		INPUT inputs[4];

		memset(inputs, 0, sizeof(inputs));

		inputs[0].type		 = INPUT_KEYBOARD;
		inputs[0].ki.wVk	 = VK_CONTROL;		//VK_SHIFT
		inputs[0].ki.wScan	 = 0x1d;			//0x2A
		inputs[0].ki.dwFlags = 0;

		inputs[1].type		 = INPUT_KEYBOARD;
		inputs[1].ki.wVk	 = 'V';				//VK_INSERT
		inputs[1].ki.wScan	 = 0x2f;			//0x52
		inputs[1].ki.dwFlags = 0;

		inputs[2].type		 = INPUT_KEYBOARD;
		inputs[2].ki.wVk	 = 'V';				//VK_INSERT
		inputs[2].ki.wScan	 = 0x2f;			//0x52
		inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

		inputs[3].type		 = INPUT_KEYBOARD;
		inputs[3].ki.wVk	 = VK_CONTROL;		//VK_SHIFT
		inputs[3].ki.wScan	 = 0x1d;			//0x2A
		inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

		SendInput(4, inputs, sizeof(INPUT));
	}
}

int SetResultString(HIMC hIMC, LPINPUTCONTEXT pIMC, PIMCONTEXT *context)
{
	LPCOMPOSITIONSTRING compose_string;
	LPCANDIDATEINFO		candidate_info;
	LPCANDIDATELIST		candidate_list;
	int length, offset;

 	length = ImmGetIMCCSize(pIMC->hCompStr);
	if ((compose_string = (LPCOMPOSITIONSTRING)ImmLockIMCC(pIMC->hCompStr)) == 0)
		return 0;

	if ((candidate_info = (LPCANDIDATEINFO)ImmLockIMCC(pIMC->hCandInfo)) == 0)
	{
		ImmUnlockIMCC(pIMC->hCompStr);
		return 0;
	}

	offset = 0;
	length = (int)_tcslen(context->result_string);

	if (length > 128)
	{
		if (!no_multi_line)				//避免Photoshop死机
		{
			if (!no_end_composition)
				SendMessage(pIMC->hWnd, WM_IME_ENDCOMPOSITION, 0, 0);

			SendMessage(pIMC->hWnd, WM_IME_NOTIFY, IMN_CLOSECANDIDATE, 1);
			UploadLongString(pIMC, context);
		}

		length = 0;
	}

	if (length > 0 && length < MAX_RESULT_LENGTH)
		memcpy((char*)compose_string + compose_string->dwResultStrOffset, context->result_string + offset, (length + 1) * sizeof(TCHAR));
	else
		*((char*)compose_string + compose_string->dwResultStrOffset) = 0;

	compose_string->dwResultStrLen	= length;
	compose_string->dwCompStrLen	= 0;
	compose_string->dwCursorPos		= 0;

	*((char*)compose_string + compose_string->dwCompStrOffset) = 0;

	ImmUnlockIMCC(pIMC->hCompStr);

	candidate_list = (LPCANDIDATELIST)((char*)candidate_info + candidate_info->dwOffset[0]);
	if (candidate_list)
	{
		candidate_list->dwCount		= 0;
		candidate_list->dwSelection = 0;
	}

	ImmUnlockIMCC(pIMC->hCandInfo);

	return 1;
}

//把最后输入的字词记录到数组里
static void Add2Recent(const TCHAR* input_string)
{
	int n = 0;

	//如果输入的词超长，就不管
	if (_tcslen(input_string) > MAX_WORD_LENGTH)
		return;

	//首先判断是否已经在数组里了
	while (n < share_segment->nCurRecent)
	{
		if (0 == _tcscmp(share_segment->szRecentResult[n], input_string))
			break;

		n++;
	}

	//如果已经存在,或者列表满了，调整列表里字词的位置，最新输入的放在最后
	//如果不存在,直接放在最后，生成菜单时是倒序的，保证最新输入的在最前面
	if ((n == share_segment->nCurRecent) && (share_segment->nCurRecent < MAX_RECENT_LENGTH))
	{
		_tcscpy(share_segment->szRecentResult[share_segment->nCurRecent], input_string);
		share_segment->nCurRecent++;
	}
	else
	{
		if (n == share_segment->nCurRecent)
			n = 0;

		while (n < share_segment->nCurRecent - 1)
		{
			_tcscpy(share_segment->szRecentResult[n], share_segment->szRecentResult[n + 1]);
			n++;
		}

		_tcscpy(share_segment->szRecentResult[n], input_string);
	}

	return;
}

//生成最新输入字词的菜单
void TrackRecent(PIMCONTEXT *context)
{
	HMENU hMenuRecent;
	int k, n, has_pos;
	POINT pos;
	HIMC hIMC;
	LPINPUTCONTEXT pIMC;
	TCHAR szItem[MAX_WORD_LENGTH + 1 + 3];

	hMenuRecent = CreatePopupMenu();
	if (NULL == hMenuRecent)
		return;

	//倒序增加菜单项
	n = share_segment->nCurRecent - 1;
	for (k = 0; k < share_segment->nCurRecent; k++)
	{
		_stprintf_s(szItem, _SizeOf(szItem), TEXT("&%0x. %s"), k, share_segment->szRecentResult[n]);
		AppendMenu(hMenuRecent, MF_STRING, n + 1, szItem);
		n--;
	}

	has_pos = 0;
	if ((!no_multi_line) && GetCaretPos(&pos) && (!(0 == pos.x && 0 == pos.y)))
	{
		if ((hIMC = GetIMC(context->ui_context->ui_window)) && (pIMC = ImmLockIMC(hIMC)))
		{
			if (pIMC->hWnd && ClientToScreen(pIMC->hWnd, &pos))
				has_pos = 1;

			ImmUnlockIMC(hIMC);
		}
	}

	if ((!has_pos) && (POS_DEFAULT_X != context->ui_context->caret_pos.x) && (POS_DEFAULT_Y != context->ui_context->caret_pos.y))
	{
		pos.x = context->ui_context->caret_pos.x;
		pos.y = context->ui_context->caret_pos.y;
		has_pos = 1;
	}

	if (!has_pos)
		return;

	pos.y = pos.y + CARET_Y_OFFSET;

	k = TrackPopupMenu(hMenuRecent, TPM_LEFTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
					   pos.x, pos.y, 0, context->ui_context->ui_window, NULL);

	//如果用户选中，通知UIWindow进行字词上屏
	if ((k > 0) && (k <= share_segment->nCurRecent))
		PostMessage(context->ui_context->ui_window, UIM_RECENT_CI, (INT_PTR)share_segment->szRecentResult[k - 1], 0);

	DestroyMenu(hMenuRecent);

	return;
}

/**	处理按键之后的事宜
 */
int PostKeyProcess(HIMC hIMC, LPINPUTCONTEXT pIMC, PIMCONTEXT *context, LPTRANSMSGLIST message_list)
{
	LPTRANSMSG  message_array, message_array_save;

	message_array = message_array_save = message_list->TransMsg;

	do
	{
		if (context->modify_flag & MODIFY_SENDBACK_CTRL)
		{
			message_array->message	= WM_KEYUP;
			message_array->wParam	= VK_CONTROL;
			message_array->lParam	= 0xC01D0001;
			message_array++;
		}

		if (context->modify_flag & MODIFY_SENDBACK_SHIFT)
		{
			message_array->message	= WM_KEYUP;
			message_array->wParam	= VK_SHIFT;
			message_array->lParam	= 0xC02A0001;
			message_array++;
		}

		if (context->modify_flag & MODIFY_STARTCOMPOSE)
		{
			if (context->modify_flag & MODIFY_COMPOSE)
				context->modify_flag &= ~MODIFY_COMPOSE;

			message_array->message	= WM_IME_STARTCOMPOSITION;
			message_array->wParam	= 0;
			message_array->lParam	= 0;
			message_array++;

			if (pim_config->support_ime_aware && !no_ime_aware)
			{	//设置写作信息
				MakeCompositionAndCandidate(pIMC, context);

				message_array->message	= WM_IME_COMPOSITION;
				message_array->wParam	= 0;
				message_array->lParam	= GCS_COMPSTR | GCS_CURSORPOS;
				message_array++;

				message_array->message	= WM_IME_NOTIFY;
				message_array->wParam	= IMN_OPENCANDIDATE;
				message_array->lParam	= 1;
				message_array++;
			}
			else
				PostMessage(context->ui_context->ui_window, UIM_NOTIFY, IMN_OPENCANDIDATE, 1);

			break;
		}

		if (context->modify_flag & MODIFY_RESULT_CONTINUE)
		{
			if (SetResultString(hIMC, pIMC, context))
			{
				message_array->message	= WM_IME_COMPOSITION;
				message_array->wParam	= 0;
				message_array->lParam	= GCS_RESULTSTR;
				message_array++;

				message_array->message	= WM_IME_ENDCOMPOSITION;
				message_array->wParam	= 0;
				message_array->lParam	= 0;
				message_array++;
			}

			ResetContext(context);

			ProcessKey(context, 0, -1, context->last_char);
			context->last_char = 0;

			message_array->message	= WM_IME_STARTCOMPOSITION;
			message_array->wParam	= 0;
			message_array->lParam	= 0;
			message_array++;

			if (pim_config->support_ime_aware && !no_ime_aware)
			{	//支持IME自动感知才对WM_IME_COMPOSITION进行设置
				MakeCompositionAndCandidate(pIMC, context);

				message_array->message	= WM_IME_COMPOSITION;
				message_array->wParam	= 0;
				message_array->lParam	= GCS_COMPSTR | GCS_CURSORPOS;
				message_array++;

				message_array->message	= WM_IME_NOTIFY;
				message_array->wParam	= IMN_CHANGECANDIDATE;
				message_array->lParam	= 1;
				message_array++;
			}
			else
				PostMessage(context->ui_context->ui_window, UIM_NOTIFY, IMN_CHANGECANDIDATE, 1);

			break;
		}

		if (context->modify_flag & MODIFY_RESULT)
		{
			if (SetResultString(hIMC, pIMC, context))
			{
				//为了让UE的写作窗口不随着上屏字符串移动，先发送一次关闭候选
				if (pim_config->support_ime_aware && !no_ime_aware)
				{
					message_array->message	= WM_IME_NOTIFY;
					message_array->lParam	= 1;
					message_array->wParam	= IMN_CLOSECANDIDATE;
					message_array++;
				}
				else
					PostMessage(context->ui_context->ui_window, UIM_NOTIFY, IMN_CLOSECANDIDATE, 1);

				message_array->message	= WM_IME_COMPOSITION;
				message_array->lParam	= GCS_RESULTSTR;// | GCS_COMPSTR | GCS_CURSORPOS;;
				message_array->wParam	= 0;
				message_array++;

				message_array->message	= WM_IME_ENDCOMPOSITION;
				message_array->lParam	= 0;
				message_array->wParam	= 0;
				message_array++;

				if (pim_config->support_ime_aware && !no_ime_aware)
				{
					message_array->message	= WM_IME_NOTIFY;
					message_array->lParam	= 1;
					message_array->wParam	= IMN_CLOSECANDIDATE;
					message_array++;
				}
				else
					PostMessage(context->ui_context->ui_window, UIM_NOTIFY, IMN_CLOSECANDIDATE, 1);
			}

			break;
		}

		if (context->modify_flag & MODIFY_COMPOSE)
		{
			if (pim_config->support_ime_aware && !no_ime_aware)
			{
				//设置写作信息
				MakeCompositionAndCandidate(pIMC, context);

				message_array->message	= WM_IME_COMPOSITION;
				message_array->wParam	= 0;
				message_array->lParam	= GCS_COMPSTR | GCS_CURSORPOS;
				message_array++;

				message_array->message	= WM_IME_NOTIFY;
				message_array->wParam	= IMN_CHANGECANDIDATE;
				message_array->lParam	= 1;
				message_array++;
			}
			else
				PostMessage(context->ui_context->ui_window, UIM_NOTIFY, IMN_CHANGECANDIDATE, 1);
		}

		if (context->modify_flag & MODIFY_STATUS)
		{
	 		message_array->message	= WM_IME_NOTIFY;
			message_array->wParam	= IMN_SETCONVERSIONMODE;
			message_array->lParam	= 0;
			message_array++;
		}

		if (context->modify_flag & MODIFY_ENDCOMPOSE)
		{
			ClearIMCResult(pIMC);

			if (pim_config->support_ime_aware && !no_ime_aware && !(context->modify_flag & MODIFY_DONT_SEND_CLEAR))
			{
				message_array->message	= WM_IME_COMPOSITION;
				message_array->lParam	= GCS_COMPSTR;// | GCS_CURSORPOS;
				message_array->wParam	= 0;
				message_array++;
			}

			message_array->message	= WM_IME_ENDCOMPOSITION;
			message_array->wParam	= 0;
			message_array->lParam	= 0;
			message_array++;

			if (pim_config->support_ime_aware && !no_ime_aware)
			{
				message_array->message	= WM_IME_NOTIFY;
				message_array->wParam	= IMN_CLOSECANDIDATE;
				message_array->lParam	= 1;
				message_array++;
			}
			else
				PostMessage(context->ui_context->ui_window, UIM_NOTIFY, IMN_CLOSECANDIDATE, 1);
		}

	}while(0);

	if (context->state == STATE_RESULT)
	{
		Add2Recent(context->result_string);  //记录到最近输入的字词列表中
		ResetContext(context);
	}

	return (int)(message_array - message_array_save);
}

UINT WINAPI ImeToAsciiEx(UINT virtual_key, UINT scan_code, CONST LPBYTE key_state, LPTRANSMSGLIST message_list, UINT fuState, HIMC hIMC)
{
	LPINPUTCONTEXT pIMC;
	PIMCONTEXT *context;

	TCHAR ch;
	int   message_count = 0;
	int   key_flag, is_shortcut_key = 0;

	//scan_code的第15位是否为1，来判断是否真实按键
	//远程桌面最小化或最大化，会触发此键（实际用户没有触发shift键）
	//这个判断不可靠，需要多尝试
	if(virtual_key == VK_SHIFT && !(scan_code == 0xc02a || scan_code == 0xc036 || scan_code == 0x2a || scan_code == 0x36 || scan_code == 0xc82a || scan_code == 0xc836))
		return 0;
	if(virtual_key == VK_CONTROL && !(scan_code == 0xc11d || scan_code == 0xc01d))
		return 0;

	Log(LOG_ID, L"virtual_key = %x, scan_code = %x", virtual_key, scan_code);
	if (!(context = LockContext(hIMC, &pIMC)))		//获得IME内部上下文指针
		return 0;

	if (!(pIMC = ImmLockIMC(hIMC)))					//获得当前输入上下文
		return 0;

	Log(LOG_ID, L"hIMC = 0x%x, pIMC=0x%x, context=0x%x", hIMC, pIMC, context);

	{
		extern int resource_thread_finished;
		while (!resource_thread_finished)
			Sleep(0);
	}

	Lock();

	context->modify_flag = 0;

	if (virtual_key == VK_CONTROL)
		context->modify_flag |= MODIFY_SENDBACK_CTRL;

	if (virtual_key == VK_SHIFT)
		context->modify_flag |= MODIFY_SENDBACK_SHIFT;

	if ((key_state[VK_CONTROL] & 0x80) && (virtual_key == VK_UP) && pim_config->trace_recent)	//直接输入最新输入的字词
	{
		if (share_segment->nCurRecent > 0)
			PostMessage(context->ui_context->ui_window, UIM_RECENT_CI, (INT_PTR)share_segment->szRecentResult[share_segment->nCurRecent - 1], 0);
	}
	else if ((key_state[VK_CONTROL] & 0x80) && (virtual_key == VK_DOWN) && pim_config->trace_recent)
	{
		TrackRecent(context);		//用户从菜单中选择最近输入过的字词
	}
	else if (virtual_key == VK_CAPITAL)
	{
		if (key_state[VK_CAPITAL] & 0x1)		//大写键按下
		{
			context->capital = 1;

			if (0 == context->input_length)
			{
				ResetContext(context);
				context->modify_flag |= MODIFY_ENDCOMPOSE;
				context->modify_flag |= MODIFY_STATUS;
			}
		}
		else
		{
			context->capital = 0;

			if (0 == context->input_length)
			{
				ResetContext(context);
				context->modify_flag |= MODIFY_STATUS;
			}
		}

		if (context->input_length)
		{
			//转换键码
			TranslateKey(virtual_key, scan_code, key_state, &key_flag, &ch, no_virtual_key);
			//处理键
			ProcessKey(context, key_flag, virtual_key, ch);

			UpdateStatusWindow(context, context->ui_context);
		}
	}
	else if (pim_config->key_change_mode == KEY_SWITCH_SHIFT && virtual_key == VK_SHIFT &&
		(pim_config->key_candidate_2nd_3rd != KEY_2ND_3RD_SHIFT || !context->input_length))
	{	//上一次的按键是shift，本次也是shift并且为抬起，则为切换中英文状态
		if (last_key == VK_SHIFT  && (key_state[VK_SHIFT] & 0x80) == 0 && !(key_state[VK_CONTROL] & 0x80))
		{
			if (pim_config->post_after_switch && context->compose_length > 0)
				SelectInputString(context, 0);

			ToggleChineseMode(context);
			SetConversionStatus(hIMC, pIMC, context);
		}
	}
	else if (pim_config->key_change_mode == KEY_SWITCH_CONTROL && virtual_key == VK_CONTROL &&
		(pim_config->key_candidate_2nd_3rd != KEY_2ND_3RD_CONTROL || !context->input_length))
	{
		if (last_key == VK_CONTROL  && (key_state[VK_CONTROL] & 0x80) == 0 && !(key_state[VK_SHIFT] & 0x80))
		{
			if (pim_config->post_after_switch && context->compose_length > 0)
				SelectInputString(context, 0);

			ToggleChineseMode(context);
			SetConversionStatus(hIMC, pIMC, context);
		}
	}
	else if ((key_state[VK_SHIFT] & 0x80) && (key_state[VK_CONTROL] & 0x80))
	{
		//软键盘快捷键
		if ((pim_config->use_key_soft_kbd) && ((virtual_key & 0xff) == pim_config->key_soft_kbd))
		{
			is_shortcut_key = 1;

			if (context->soft_keyboard)
				context->state = STATE_SOFTKBD;

			//必须判断状态条是否出现，否则不显示这个窗口
			//用于DOS窗口的处理
			context->soft_keyboard = !context->soft_keyboard;
			if (context && context->ui_context)
			{
				if (!host_is_console)
					PostMessage(context->ui_context->ui_window, UIM_MODE_CHANGE, 0, 0);
				else		//1，1表示不需要显示（DOS窗口不能显示软键盘）
					PostMessage(context->ui_context->ui_window, UIM_MODE_CHANGE, 1, 1);
			}
		}
		//简繁切换快捷键
		else if ((pim_config->use_key_jian_fan) && ((virtual_key & 0xff) == pim_config->key_jian_fan))
		{
			is_shortcut_key = 1;
			ToggleFanJian(context);
		}
		//双拼切换快捷键
		else if ((pim_config->use_key_quan_shuang_pin) && ((virtual_key & 0xff) == pim_config->key_quan_shuang_pin))
		{
			is_shortcut_key = 1;
			ToggleQuanShuang(context);
		}
		//状态栏切换快捷键
		else if ((pim_config->use_key_status_window) && ((virtual_key & 0xff) == pim_config->key_status_window))
		{
			is_shortcut_key = 1;
			ToggleShowStatusWindow(context);
		}
		//英文输入法快捷键
		else if ((pim_config->use_english_input) && (pim_config->use_key_english_input) && ((virtual_key & 0xff) == pim_config->key_english_input))
		{
			if (!IsFullScreen())
			{
				is_shortcut_key = 1;
				ToggleEnglishInput(context);
			}
		}

		if (!is_shortcut_key)
		{
			//转换键码
			TranslateKey(virtual_key, scan_code, key_state, &key_flag, &ch, no_virtual_key);
			//处理键
			ProcessKey(context, key_flag, virtual_key, ch);
		}
	}
	else
	{
		//转换键码
		TranslateKey(virtual_key, scan_code, key_state, &key_flag, &ch, no_virtual_key);
		//处理键
		ProcessKey(context, key_flag, virtual_key, ch);
	}

	last_key = virtual_key;
	message_count = PostKeyProcess(hIMC, pIMC, context, message_list);
	context->last_digital = 0;

	ImmUnlockIMC(hIMC);

	Unlock();

	Log(LOG_ID, L"context->input_length = %d", context->input_length);

	return message_count;
}

BOOL WINAPI ImeRegisterWord(LPCTSTR  lpszReading, DWORD  dwStyle, LPCTSTR  lpszString)
{
	Log(LOG_ID, L"被调用");
	return 1;
}

BOOL WINAPI ImeUnregisterWord(LPCTSTR  lpszReading, DWORD  dwStyle, LPCTSTR  lpszString)
{
	Log(LOG_ID, L"被调用");
	return 1;
}

UINT WINAPI ImeGetRegisterWordStyle(UINT  nItem, LPSTYLEBUF  lpStyleBuf)
{
	Log(LOG_ID, L"被调用");
	return 0;
}

UINT WINAPI ImeEnumRegisterWord(REGISTERWORDENUMPROC lpfnEnumProc, LPCTSTR lpszReading, DWORD dwStyle, LPCTSTR lpszString, LPVOID lpData)
{
	Log(LOG_ID, L"被调用");
	return 0;
}

DWORD WINAPI ImeGetImeMenuItems(HIMC hIMC, DWORD dwFlags, DWORD dwType, LPIMEMENUITEMINFO lpImeParentMenu, LPIMEMENUITEMINFO lpImeMenu, DWORD dwSize)
{
	Log(LOG_ID, L"被调用");
	return 0;
}

LRESULT WINAPI ImeEscape(HIMC  hIMC, UINT uEscape, LPVOID lpData)
{
	Log(LOG_ID, L"被调用");
	return 0;
}

void GenerateImeMessage(HIMC hIMC, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LPINPUTCONTEXT	pIMC;

	Log(LOG_ID, L"生成IME消息, hIMC=%x, message=%x, wParam=%x, lParam=%x", hIMC, uMsg, wParam, lParam);

	if (IsIME64())
	{
		PDWORD64 message_buffer;

		pIMC = ImmLockIMC(hIMC);
		if (!pIMC)
			return;

		pIMC->hMsgBuf = ImmReSizeIMCC(pIMC->hMsgBuf, (pIMC->dwNumMsgBuf + 1) * sizeof(DWORD64) * 3);
		if (!pIMC->hMsgBuf)
		{
			ImmUnlockIMC(hIMC);
			return;
		}

		message_buffer = (PDWORD64) ImmLockIMCC(pIMC->hMsgBuf);
		if (!message_buffer)
		{
			ImmUnlockIMC(hIMC);
			return;
		}

		message_buffer += pIMC->dwNumMsgBuf * 3;

		pIMC->dwNumMsgBuf++;

		*message_buffer++ = (DWORD64)uMsg;
		*message_buffer++ = (DWORD64)wParam;
		*message_buffer++ = (DWORD64)lParam;
	}
	else
	{
		LPDWORD	message_buffer;

		pIMC = ImmLockIMC(hIMC);
		if (!pIMC)
			return;

		pIMC->hMsgBuf = ImmReSizeIMCC(pIMC->hMsgBuf, (pIMC->dwNumMsgBuf + 1) * sizeof(DWORD) * 3);
		if (!pIMC->hMsgBuf)
		{
			ImmUnlockIMC(hIMC);
			return;
		}

		message_buffer = (LPDWORD) ImmLockIMCC(pIMC->hMsgBuf);
		if (!message_buffer)
		{
			ImmUnlockIMC(hIMC);
			return;
		}

		message_buffer += pIMC->dwNumMsgBuf * 3;

		pIMC->dwNumMsgBuf++;

		*message_buffer++ = uMsg;
		*message_buffer++ = (DWORD)wParam;
		*message_buffer++ = (DWORD)lParam;
	}

	ImmUnlockIMCC(pIMC->hMsgBuf);
	ImmUnlockIMC(hIMC);
	ImmGenerateMessage(hIMC);
}
