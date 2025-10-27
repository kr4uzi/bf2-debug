#pragma once
#ifndef _BF2PY_SIM_H_
#define _BF2PY_SIM_H_

#include <vector>
#include <string>
#include <memory>
#include <expected>

namespace bf2py {
	class dyn_lib {
		struct dyn_lib_unloader {
			void operator()(void* handle);
		};
		std::unique_ptr<void, dyn_lib_unloader> _handle;

	public:
		dyn_lib(const char* path);
		operator bool() const noexcept { return _handle.operator bool(); };
	};

	class bf2simulator {
		volatile bool _running = false;
		std::vector<std::string> _dlls;
		std::string _mod = "bf2";
		std::string _adminScript = "default";
		void* _adminInit = nullptr, * _adminUpdate = nullptr, * _adminShutdown = nullptr;

	public:
		std::vector<std::string> dlls() const { return _dlls; }
		void dlls(const std::vector<std::string>& dlls) { _dlls = dlls; }

		// dice::hfe::GameServer::init
		int run();
		void stop();

	private:
		// dice::hfe::python::PythonHost::initialize
		bool PythonHost_initialize();
		// dice::hfe::python::createHostModule
		bool createHostModule();
		// dice::hfe::python::PythonHost::initializeAdminMod
		bool PythonHost_initializeAdminMod();
		// dice::hfe::python::PythonHost::shutdown
		bool PythonHost_shutdown();
	};
}

#endif
