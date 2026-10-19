#include "pch.h"
#include "ProcessTypes.h"
#include <TlHelp32.h>
#include <winver.h>
#include <Softpub.h>
#include <wintrust.h>
#include "SnapshotHandle.h"
#include "Win32Api.h"
#include "VersionResourceReader.h"
#include "AuthenticodeVerifier.h"
#include "ProcessEnumerator.h"

namespace {
	class ProcessHandle {
	private:
		HANDLE m_handle = nullptr;
	public:
		explicit ProcessHandle(HANDLE h) : m_handle(h) {}
		~ProcessHandle() {
			if (m_handle != nullptr) {
				GetWin32Api().CloseHandle(m_handle);
			}

		}

		ProcessHandle(const ProcessHandle&) = delete;
		ProcessHandle& operator=(const ProcessHandle&) = delete;

		bool is_valid() const noexcept {
			return m_handle != nullptr;
		}

		HANDLE get() const noexcept {
			return m_handle;
		}
	};

	std::optional<std::uint64_t> QueryCreationTimeByHandle(std::uint32_t pid) {
		HANDLE handle = GetWin32Api().OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		ProcessHandle process(handle);
		if (!process.is_valid()) return std::nullopt;
		FILETIME creation{}, exit{}, kernel{}, user{};
		if (!GetWin32Api().GetProcessTimes(process.get(), &creation, &exit, &kernel, &user)) return std::nullopt;
		return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
	}

	Field<SignatureStatus> QuerySignature(const std::wstring& image_path) {
		return AuthenticodeVerifier{}.verify(image_path);
	}
	
	struct ProcessDetails {
		Field<std::wstring> image_path;
		Field<std::uint64_t> creation_time;
		Field<std::wstring> internal_name;
		Field<CollectionResult<ModuleRecord>> modules;
		Field<SignatureStatus> signature;
		ObservationConsistency observation_consistency = ObservationConsistency::snapshot_only;
	};

	Field<std::wstring> QueryInternalName(const std::wstring& image_path) {
		return VersionResourceReader{}.read(image_path);
	}

	Field<CollectionResult<ModuleRecord>> QueryProcessModules(std::uint32_t pid, const StopToken* stop_token) {
		CollectionResult<ModuleRecord> result;
		HANDLE h = INVALID_HANDLE_VALUE;
		DWORD snapshot_error = ERROR_SUCCESS;
		for (int attempt = 0; attempt != 3; ++attempt) {
			h = GetWin32Api().CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
			if (h != INVALID_HANDLE_VALUE) break;
			snapshot_error = GetWin32Api().GetLastErrorValue();
			if (snapshot_error != ERROR_BAD_LENGTH || attempt == 2) break;
		}
		SnapshotHandle snapshot(h);
		if (!snapshot.is_valid()) {
			const DWORD error = snapshot_error;
			result.state = ResultState::failed;
			result.diagnostics.push_back(ErrorInfo{
				"CreateToolhelp32Snapshot",
				static_cast<std::uint32_t>(error),
				"Não foi possível criar o snapshot de módulos do processo."
				});
			return Field<CollectionResult<ModuleRecord>>::unavailable(result.diagnostics.back());
		}
		MODULEENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		if (!GetWin32Api().Module32FirstW(snapshot.get(), &entry)) {
			const DWORD error = GetWin32Api().GetLastErrorValue();
			if (error != ERROR_NO_MORE_FILES) {
				result.state = ResultState::failed;
				result.diagnostics.push_back(ErrorInfo{
					"Module32FirstW",
					static_cast<std::uint32_t>(error),
					"Não foi possível obter o primeiro módulo do processo."
					});
				return Field<CollectionResult<ModuleRecord>>::unavailable(result.diagnostics.back());
			}
			else {
				return Field<CollectionResult<ModuleRecord>>::available(result);
			}
		}
		do {
			if (stop_token != nullptr && stop_token->stop_requested()) {
				result.state = ResultState::partial;
				result.diagnostics.push_back(ErrorInfo{
					"StopToken", ERROR_CANCELLED,
					"A enumeração de módulos foi cancelada pelo consumidor.",
					ErrorDomain::cancellation
				});
				break;
			}
			result.items.push_back(ModuleRecord{
				Field<std::wstring>::available(entry.szModule),
				Field<std::wstring>::available(entry.szExePath),
				reinterpret_cast<std::uint64_t>(entry.modBaseAddr)
				});
		} while (GetWin32Api().Module32NextW(snapshot.get(), &entry));
		const DWORD error = GetWin32Api().GetLastErrorValue();
		if (error != ERROR_NO_MORE_FILES) {
			result.state = ResultState::partial;
			result.diagnostics.push_back(ErrorInfo{
				"Module32NextW",
				static_cast<std::uint32_t>(error),
				"Não foi possível obter todos os módulos do processo."
				});
		}
		return Field<CollectionResult<ModuleRecord>>::available(result);
	}

	ProcessDetails QueryProcessDetails(std::uint32_t pid, const InspectionOptions& options) {
		if (pid == 0 || pid == 4) {
			const ErrorInfo info{
				"OpenProcess",
				ERROR_ACCESS_DENIED,
				"PID reservado: a inspeção por handle não é solicitada."
			};
			ProcessDetails details;
			if (options.include_image_path) details.image_path = Field<std::wstring>::access_denied(info);
			if (options.include_internal_name) details.internal_name = Field<std::wstring>::access_denied(info);
			if (options.include_modules) details.modules = Field<CollectionResult<ModuleRecord>>::access_denied(info);
			if (options.include_signature) details.signature = Field<SignatureStatus>::access_denied(info);
			details.creation_time = Field<std::uint64_t>::access_denied(info);
			return details;
		}
		HANDLE h = GetWin32Api().OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
		ProcessHandle process_handle(h);
		ProcessDetails details;

		if (!process_handle.is_valid()) {
			const DWORD error = GetWin32Api().GetLastErrorValue();
			const ErrorInfo info{
				"OpenProcess",
				static_cast<std::uint32_t>(error),
				error == ERROR_INVALID_PARAMETER
					? "OpenProcess recebeu parâmetro inválido para PID " + std::to_string(pid) + "."
					: "Não foi possível abrir o processo para consulta de informações."
			};

			if (options.include_image_path) {
				details.image_path = error == ERROR_ACCESS_DENIED
					? Field<std::wstring>::access_denied(info)
					: Field<std::wstring>::unavailable(info);
			}
			if (options.include_internal_name) {
				details.internal_name = Field<std::wstring>::unavailable(info);
			}
			if (options.include_modules) {
				details.modules = Field<CollectionResult<ModuleRecord>>::unavailable(info);
			}
			if (options.include_signature) {
				details.signature = Field<SignatureStatus>::unavailable(info);
			}
			details.creation_time = error == ERROR_ACCESS_DENIED
				? Field<std::uint64_t>::access_denied(info)
				: Field<std::uint64_t>::unavailable(info);
			return details;
		}
		details.observation_consistency = ObservationConsistency::handle_only;

		if (options.include_image_path) {
			std::wstring path(_MAX_PATH, L'\0');
			for (;;) {
				DWORD length = static_cast<DWORD>(path.size());
				if (GetWin32Api().QueryFullProcessImageNameW(process_handle.get(), 0, path.data(), &length)) {
					path.resize(length);
					details.image_path = Field<std::wstring>::available(std::move(path));
					break;
				}

				const DWORD error = GetWin32Api().GetLastErrorValue();
				if (error == ERROR_INSUFFICIENT_BUFFER && path.size() < 32768) {
					const std::size_t next_size = (std::min)(path.size() * 2, static_cast<std::size_t>(32768));
					path.resize(next_size);
					continue;
				}

				const ErrorInfo info{
					"QueryFullProcessImageNameW",
					static_cast<std::uint32_t>(error),
					"Não foi possível obter o caminho completo da imagem do processo."
				};
				details.image_path = error == ERROR_ACCESS_DENIED
					? Field<std::wstring>::access_denied(info)
					: Field<std::wstring>::unavailable(info);
				break;
			}
		}

		FILETIME creation_time{};
		FILETIME exit_time{};
		FILETIME kernel_time{};
		FILETIME user_time{};
		if (GetWin32Api().GetProcessTimes(process_handle.get(), &creation_time, &exit_time, &kernel_time, &user_time)) {
			const std::uint64_t ticks =
				(static_cast<std::uint64_t>(creation_time.dwHighDateTime) << 32) |
				creation_time.dwLowDateTime;
			details.creation_time = Field<std::uint64_t>::available(ticks);
		}
		else {
			const DWORD error = GetWin32Api().GetLastErrorValue();
			const ErrorInfo info{
				"GetProcessTimes",
				static_cast<std::uint32_t>(error),
				"Não foi possível obter o horário de criação do processo."
			};
			details.creation_time = error == ERROR_ACCESS_DENIED
				? Field<std::uint64_t>::access_denied(info)
				: Field<std::uint64_t>::unavailable(info);
		}

		if (options.include_internal_name) {
			if (details.image_path.state() == FieldState::available && details.image_path.value().has_value()) {
				details.internal_name = QueryInternalName(*details.image_path.value());
			}
			else {
				details.internal_name = Field<std::wstring>::unavailable(ErrorInfo{
					"QueryInternalName",
					0,
					"Não foi possível obter o nome interno sem um caminho de imagem disponível."
					});
			}
		}

		if (options.include_modules) {
			USHORT process_machine = IMAGE_FILE_MACHINE_UNKNOWN;
			USHORT native_machine = IMAGE_FILE_MACHINE_UNKNOWN;
			if (!GetWin32Api().IsWow64Process2(process_handle.get(), &process_machine, &native_machine)) {
				details.modules = Field<CollectionResult<ModuleRecord>>::available_unverified(
					CollectionResult<ModuleRecord>{ {}, ResultState::partial, {
						ErrorInfo{
							"IsWow64Process2", GetWin32Api().GetLastErrorValue(),
							"A arquitetura do processo não pôde ser comprovada.",
							ErrorDomain::validation
						}
					} });
			}
#if defined(_WIN64)
			else if (process_machine != IMAGE_FILE_MACHINE_UNKNOWN) {
				details.modules = Field<CollectionResult<ModuleRecord>>::unsupported_architecture(
					ErrorInfo{
						"IsWow64Process2", ERROR_BAD_EXE_FORMAT,
						"A enumeração de módulos cross-bitness não é suportada nesta build x64.",
						ErrorDomain::validation
					});
			}
#endif
			if (details.modules.state() != FieldState::unsupported_architecture &&
				details.modules.state() != FieldState::available_unverified) {
			FILETIME before_creation{}, before_exit{}, before_kernel{}, before_user{};
			const bool identity_before = GetWin32Api().GetProcessTimes(
				process_handle.get(), &before_creation, &before_exit, &before_kernel, &before_user) != FALSE;
			const DWORD wait_before = GetWin32Api().WaitForSingleObject(process_handle.get(), 0);
			details.modules = QueryProcessModules(pid, options.stop_token);
			FILETIME after_creation{}, after_exit{}, after_kernel{}, after_user{};
			const bool identity_after = GetWin32Api().GetProcessTimes(
				process_handle.get(), &after_creation, &after_exit, &after_kernel, &after_user) != FALSE;
			const DWORD wait_after = GetWin32Api().WaitForSingleObject(process_handle.get(), 0);
			if (wait_before == WAIT_OBJECT_0 || wait_after == WAIT_OBJECT_0) {
				if (details.modules.value().has_value()) {
					CollectionResult<ModuleRecord> modules = *details.modules.value();
					modules.state = ResultState::partial;
					modules.diagnostics.push_back(ErrorInfo{
						"WaitForSingleObject", ERROR_PROCESS_ABORTED,
						"O processo encerrou durante a enumeração de módulos.",
						ErrorDomain::win32
					});
					details.modules = Field<CollectionResult<ModuleRecord>>::available_unverified(std::move(modules));
				}
			}
			const bool same_identity = identity_before && identity_after &&
				before_creation.dwHighDateTime == after_creation.dwHighDateTime &&
				before_creation.dwLowDateTime == after_creation.dwLowDateTime;
			if (!same_identity && details.modules.value().has_value()) {
				CollectionResult<ModuleRecord> modules = *details.modules.value();
				modules.state = ResultState::partial;
				modules.diagnostics.push_back(ErrorInfo{
					"GetProcessTimes", ERROR_INVALID_DATA,
					"A identidade do processo não pôde ser comprovada antes e depois da enumeração de módulos.",
					ErrorDomain::validation
				});
				details.modules = Field<CollectionResult<ModuleRecord>>::available_unverified(
					std::move(modules));
			}
			}
		}

		if (options.include_signature) {
			if (details.image_path.state() == FieldState::available && details.image_path.value().has_value()) {
				details.signature = QuerySignature(*details.image_path.value());
			}
			else {
				details.signature = Field<SignatureStatus>::unavailable(ErrorInfo{
					"QuerySignature",
					0,
					"Não foi possível verificar a assinatura sem um caminho de imagem disponível."
					});
			}
		}
		return details;
	}

	ProcessDetails InspectProcess(
		std::uint32_t pid,
		const InspectionOptions& options) {
		return QueryProcessDetails(pid, options);
	}
}

QueryResult QueryProcessFiltered(const InspectionOptions& options, std::optional<std::uint32_t> pid);

QueryResult QueryProcessByPid(std::uint32_t pid, const InspectionOptions& options) {
	QueryResult res = QueryProcessFiltered(options, pid);

	if (res.state == ResultState::failed) {
		return res;
	}

	/*for (std::size_t i = 0; i < res.processes.size(); ++i) {
		if (res.processes[i].pid == pid) {
			QueryResult single_res;
			single_res.processes.push_back(res.processes[i]);
			return single_res;
		}
	}
	
	return QueryResult{};*/

	std::erase_if(res.processes, [pid](const ProcessRecord& process) {
		return process.pid != pid;
		});

	return res;
}

QueryResult QueryProcessByName(std::wstring name, const InspectionOptions& options) {
	if (name.empty()) {
		QueryResult result;
		result.state = ResultState::failed;
		result.fatal_error = ErrorInfo{
			"by_name",
			ERROR_INVALID_PARAMETER,
			"O nome do processo não pode ser vazio.",
			ErrorDomain::validation
		};
		return result;
	}
	QueryResult res = QueryProcess(options);

	if (res.state == ResultState::failed) {
		return res;
	}

	std::erase_if(res.processes, [name](const ProcessRecord& process) {
		return GetWin32Api().CompareStringOrdinal(
			process.name.value()->c_str(), -1,
			name.c_str(), -1,
			TRUE) != CSTR_EQUAL;
		});

	return res;
}

QueryResult QueryProcessByFullPath(std::wstring full_path, const InspectionOptions& options) {
	const bool has_drive = full_path.size() >= 3 && full_path[1] == L':' &&
		(full_path[2] == L'\\' || full_path[2] == L'/');
	const bool is_unc = full_path.starts_with(L"\\\\");
	const bool is_extended = full_path.starts_with(L"\\\\?\\");
	if (full_path.empty() || (!has_drive && !is_unc && !is_extended)) {
		QueryResult result;
		result.state = ResultState::failed;
		result.fatal_error = ErrorInfo{
			"by_full_path",
			ERROR_INVALID_NAME,
			"O caminho deve ser absoluto, UNC ou de formato \\?\\.",
			ErrorDomain::validation
		};
		return result;
	}
	QueryResult res = QueryProcess(InspectionOptions{
		true, // garante que o caminho da imagem seja incluído na consulta
		options.include_internal_name,
		options.include_signature,
		options.include_modules,
		options.allow_remote_file_inspection,
		options.stop_token
		});

	if (res.state == ResultState::failed) {
		return res;
	}
	
	std::erase_if(res.processes, [full_path, &res](const ProcessRecord& process) {
		if (process.image_path.state() != FieldState::available) {
			++res.unevaluable_processes;
			res.state = ResultState::partial;
			return true; // remove se não tiver caminho de imagem disponível
		}

		return GetWin32Api().CompareStringOrdinal(
			process.image_path.value()->c_str(), -1,
			full_path.c_str(), -1,
			TRUE) != CSTR_EQUAL;
		});

	return res;
}


QueryResult QueryProcessFiltered(const InspectionOptions& options, std::optional<std::uint32_t> pid) {
	QueryResult res;

	// Define quantos processos podem ser analisados ao mesmo tempo.
	// Se o usuário informar zero, usamos um processo por vez para evitar um valor inválido.
	HANDLE h = GetWin32Api().CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	SnapshotHandle snapshot(h);

	if (!snapshot.is_valid()) {
		const DWORD error = GetWin32Api().GetLastErrorValue();

		res.state = ResultState::failed;
		res.fatal_error = ErrorInfo{
			"CreateToolhelp32Snapshot",
			static_cast<std::uint32_t>(error),
			"Não foi possível criar o snapshot de processos."
		};

		return res;
	}

	PROCESSENTRY32W entry{};

	entry.dwSize = sizeof(entry);

	if (!GetWin32Api().Process32FirstW(snapshot.get(), &entry)) {
		const DWORD error = GetWin32Api().GetLastErrorValue();

		if (error == ERROR_NO_MORE_FILES) {
			return res; // ainda está complete e vazio
		}

		res.state = ResultState::failed;
		res.fatal_error = ErrorInfo{
			"Process32FirstW",
			static_cast<std::uint32_t>(error),
			"Não foi possível obter o primeiro processo."
		};

		return res;
	}

	// Coloca o resultado da análise no processo correto e registra eventuais problemas.
	auto apply_details = [&res, &options](std::size_t process_index, ProcessDetails details) {
		ProcessRecord& process = res.processes[process_index];
		// Transfere os dados obtidos para o registro do processo.
		// Isso evita copiar textos e listas grandes sem necessidade.
		process.image_path = std::move(details.image_path);
		process.creation_time_100ns = std::move(details.creation_time);
		process.internal_name = std::move(details.internal_name);
		process.modules = std::move(details.modules);
		process.signature = std::move(details.signature);
		process.observation_consistency = details.observation_consistency;

		// Se um campo tiver uma mensagem de erro, adiciona essa mensagem ao resultado geral.
		auto add_field_diagnostic = [&res](const auto& field) {
			if (field.error().has_value()) {
				res.diagnostics.push_back(*field.error());
			}
		};

		if (options.include_image_path && process.image_path.state() != FieldState::available) {
			res.state = ResultState::partial;
			add_field_diagnostic(process.image_path);
		}
		if (options.include_signature && process.signature.state() != FieldState::available) {
			res.state = ResultState::partial;
			add_field_diagnostic(process.signature);
		}
		if (process.creation_time_100ns.state() != FieldState::available) {
			res.state = ResultState::partial;
			add_field_diagnostic(process.creation_time_100ns);
		}
		if (options.include_internal_name && process.internal_name.state() != FieldState::available) {
			res.state = ResultState::partial;
			add_field_diagnostic(process.internal_name);
		}
		if (options.include_modules) {
			if (process.modules.state() != FieldState::available) {
				res.state = ResultState::partial;
				add_field_diagnostic(process.modules);
			}
			else if (process.modules.value()->state != ResultState::complete) {
				res.state = ResultState::partial;
				for (const ErrorInfo& error : process.modules.value()->diagnostics) {
					res.diagnostics.push_back(error);
				}
			}
		}
	};

	BOOL has_next = TRUE;
	do {
		if (options.stop_token != nullptr && options.stop_token->stop_requested()) {
			res.state = ResultState::partial;
			res.diagnostics.push_back(ErrorInfo{
				"StopToken",
				ERROR_CANCELLED,
				"A enumeração foi cancelada pelo consumidor."
			});
			break;
		}
		// Primeiro salvamos os dados básicos encontrados no snapshot.
		// Os dados extras ficam como "não solicitados" até a análise terminar.
		res.processes.push_back(ProcessRecord{
			entry.th32ProcessID,
			Field<std::uint32_t>::available(entry.th32ParentProcessID),
			Field<std::wstring>::available(entry.szExeFile),
			Field<std::wstring>::not_requested(),
			Field<std::wstring>::not_requested(),
			Field<std::uint64_t>::not_requested(),
			Field<std::wstring>::not_requested(),
			Field<CollectionResult<ModuleRecord>>::not_requested(),
			Field<SignatureStatus>::not_requested()
		});

		// Na enumeração geral, analisamos todos os processos.
		// Em uma busca por PID, analisamos somente o processo procurado.
		// Os demais continuam na lista para podermos encontrar o nome do pai.
		if (!pid.has_value() || entry.th32ProcessID == *pid) {
			apply_details(res.processes.size() - 1, InspectProcess(entry.th32ProcessID, options));
		}
		has_next = GetWin32Api().Process32NextW(snapshot.get(), &entry);
	} while (has_next);

	if (!has_next) {
		const DWORD error = GetWin32Api().GetLastErrorValue();
		if (error != ERROR_NO_MORE_FILES) {
			res.state = ResultState::partial;
			res.diagnostics.push_back(ErrorInfo{
				"Process32NextW",
				static_cast<std::uint32_t>(error),
				"Não foi possível obter todos os processos."
				});
		}
	}

	for (ProcessRecord& process : res.processes) {
		if (process.ppid.state() == FieldState::available) {
			auto it = std::find_if(res.processes.begin(), res.processes.end(),
				[ppid = *process.ppid.value(), pid = process.pid](const ProcessRecord& p) {
					return p.pid == ppid && p.pid != pid;
				});
			if (it != res.processes.end()) {
				if (it->creation_time_100ns.state() == FieldState::available &&
					it->creation_time_100ns.value().has_value()) {
					const std::optional<std::uint64_t> current_parent_creation = QueryCreationTimeByHandle(it->pid);
					if (current_parent_creation.has_value() &&
						current_parent_creation.value() != it->creation_time_100ns.value().value()) {
						const ErrorInfo mismatch{
							"GetProcessTimes", ERROR_INVALID_DATA,
							"O PID do processo pai foi reutilizado durante a observação.",
							ErrorDomain::validation
						};
						process.parent_name = Field<std::wstring>::failure(mismatch);
						process.observation_consistency = ObservationConsistency::mismatch_detected;
						continue;
					}
				}
				process.parent_name = it->name;
			}
			else {
				const ErrorInfo diagnostic{
					"Process32FirstW/Process32NextW",
					0,
					"O processo pai não foi observado na consulta atual; o nome não foi confirmado.",
					ErrorDomain::validation
				};
				process.parent_name = Field<std::wstring>::available_unverified(
					L"<pai não observado>", diagnostic);
			}
		}
		else {
			process.parent_name = Field<std::wstring>::unavailable(ErrorInfo{
				"Process32FirstW/Process32NextW",
				0,
				"Não foi possível obter o PID do processo pai."
				});
		}
	}

	if (pid.has_value()) {
		std::erase_if(res.processes, [pid](const ProcessRecord& process) {
			return process.pid != *pid;
		});
	}

	return res;
}

QueryResult QueryProcess(const InspectionOptions& options) {
	return QueryProcessFiltered(options, std::nullopt);
}