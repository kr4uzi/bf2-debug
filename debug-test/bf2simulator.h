#pragma once
#ifndef _BF2PY_SIM_H_
#define _BF2PY_SIM_H_

#include <vector>
#include <string>

namespace bf2py {
	class bf2simulator {
		volatile bool _running = false;
		std::vector<std::string> _dlls;

	public:
		std::vector<std::string> dlls() const { return _dlls; }
		void dlls(const std::vector<std::string>& dlls) { _dlls = dlls; }

		int run(const std::string& mod = "bf2", const std::string& adminScript = "default");
		void stop();

	private:
		int init();
	};
}

#endif
