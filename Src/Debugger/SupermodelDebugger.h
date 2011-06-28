#ifdef SUPERMODEL_DEBUGGER
#ifndef INCLUDED_SUPERMODELDEBUGGER_H
#define INCLUDED_SUPERMODELDEBUGGER_H

#include "ConsoleDebugger.h"

#include <stdarg.h>

#define MODEL3_STATEFILE_VERSION 0

class CModel3;
class CInputs;
class CInput;

namespace Debugger
{
	class CCPUDebug;

	/*
	 * Console-based debugger used by Supermodel.
	 */
	class CSupermodelDebugger : public CConsoleDebugger
	{
	private:
		::CModel3 *m_model3;
		::CInputs *m_inputs;
		::CLogger *m_logger;

		bool InputIsValid(CInput *input);

		void ListInputs();

	protected:
		void WaitCommand(CCPUDebug *cpu);

		bool ProcessToken(const char *token, const char *cmd);

	public:
		CSupermodelDebugger(::CModel3 *model3, ::CInputs *inputs, ::CLogger *logger);

		void Attached();

		void Detaching();
		
		bool LoadModel3State(const char *fileName);

		bool SaveModel3State(const char *fileName);

		void DebugLog(const char *fmt, va_list vl);
		
		void InfoLog(const char *fmt, va_list vl);
		
		void ErrorLog(const char *fmt, va_list vl);
	};
}

#endif	// INCLUDED_SUPERMODELDEBUGGER_H
#endif  // SUPERMODEL_DEBUGGER