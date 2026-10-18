#include "pch.h"
#include "ProcessTypes.h"
#include <TlHelp32.h>
#include <winver.h>
#include <Softpub.h>
#include <wintrust.h>
#include "SnapshotHandle.h"
#include "ProcessEnumerator.h"
#include <future>

namespace {
	class ProcessHandle {
	private:
		HANDLE m_handle = nullptr;
	public:
		explicit ProcessHandle(HANDLE h) : m_handle(h) {}
		~ProcessHandle() {
			if (m_handle != nullptr) {
				CloseHandle(m_handle);
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

	Field<SignatureStatus> QuerySignature(const std::wstring& image_path) {
		SignatureStatus status{
			SignaturePresence::unknown,
			TrustState::indeterminate,
			0,
			std::nullopt,
			L"WTD_REVOCATION_CHECK_END_CERT"
		};

		WINTRUST_FILE_INFO file_info{};
		file_info.cbStruct = sizeof(file_info);
		file_info.pcwszFilePath = image_path.c_str();

		WINTRUST_DATA trust_data{};
		trust_data.cbStruct = sizeof(trust_data);
		trust_data.dwUIChoice = WTD_UI_NONE;
		trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
		trust_data.dwUnionChoice = WTD_CHOICE_FILE;
		trust_data.pFile = &file_info;
		trust_data.dwStateAction = WTD_STATEACTION_VERIFY;
		trust_data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

		const GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
		const LONG verify_status = WinVerifyTrust(nullptr, const_cast<GUID*>(&action), &trust_data);
		status.wintrust_status = verify_status;

		trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
		WinVerifyTrust(nullptr, const_cast<GUID*>(&action), &trust_data);

		if (verify_status == ERROR_SUCCESS) {
			status.presence = SignaturePresence::present;
			status.trust = TrustState::valid;
		}
		else if (verify_status == TRUST_E_NOSIGNATURE) {
			status.presence = SignaturePresence::absent;
			status.trust = TrustState::not_evaluated;
		}

		return Field<SignatureStatus>::available(std::move(status));
	}
	
	struct ProcessDetails {
		Field<std::wstring> image_path;
		Field<std::uint64_t> creation_time;
		Field<std::wstring> internal_name;
		Field<CollectionResult<ModuleRecord>> modules;
		Field<SignatureStatus> signature;
	};

	struct PendingInspection {
		std::size_t process_index;
		std::uint32_t pid;
		std::future<ProcessDetails> result;
	};

	Field<std::wstring> QueryInternalName(const std::wstring& image_path) {
		DWORD handle = 0;
		// Obtém o tamanho necessário para o bloco de informações de versão do arquivo PE.
		// O parâmetro `handle` é reservado pela API e não representa um HANDLE Win32 aberto.
		const DWORD size = GetFileVersionInfoSizeW(image_path.c_str(), &handle);

		if (size == 0) {
			const DWORD error = GetLastError();
			return Field<std::wstring>::unavailable(ErrorInfo{
				"GetFileVersionInfoSizeW",
				static_cast<std::uint32_t>(error),
				"Não foi possível obter o tamanho das informações de versão."
				});
		}

		std::vector<std::byte> version_data(size);
		// Carrega na memória o recurso VERSIONINFO completo do executável.
		// O buffer precisa ter exatamente, no mínimo, o tamanho retornado acima.
		if (!GetFileVersionInfoW(image_path.c_str(), 0, size, version_data.data())) {
			const DWORD error = GetLastError();
			return Field<std::wstring>::unavailable(ErrorInfo{
				"GetFileVersionInfoW",
				static_cast<std::uint32_t>(error),
				"Não foi possível obter as informações de versão do executável."
				});
		}

		struct LanguageAndCodePage {
			WORD language;
			WORD code_page;
		};

		void* translation_data = nullptr;
		UINT translation_size = 0;
		// Localiza a tabela de pares idioma/code page que identifica as tabelas
		// StringFileInfo disponíveis no recurso de versão.
		if (!VerQueryValueW(
			version_data.data(),
			L"\\VarFileInfo\\Translation",
			&translation_data,
			&translation_size) ||
			translation_size < sizeof(LanguageAndCodePage)) {
			return Field<std::wstring>::unavailable(ErrorInfo{
				"VerQueryValueW",
				0,
				"O executável não possui uma tabela de tradução de versão."
				});
		}

		const auto* translations = static_cast<const LanguageAndCodePage*>(translation_data);
		const std::size_t translation_count = translation_size / sizeof(LanguageAndCodePage);
		for (std::size_t i = 0; i < translation_count; ++i) {
			wchar_t sub_block[64]{};
			// Monta o caminho do valor InternalName para a combinação de idioma
			// e página de código encontrada na tabela Translation.
			swprintf_s(
				sub_block,
				L"\\StringFileInfo\\%04x%04x\\InternalName",
				translations[i].language,
				translations[i].code_page);

			void* value_data = nullptr;
			UINT value_size = 0;
			// Consulta o valor textual dentro do bloco VERSIONINFO carregado.
			// O valor retornado normalmente inclui o terminador NUL, por isso ele
			// é removido ao construir a std::wstring abaixo.
			if (VerQueryValueW(version_data.data(), sub_block, &value_data, &value_size) &&
				value_data != nullptr && value_size > 1) {
				std::wstring internal_name(
					static_cast<const wchar_t*>(value_data),
					value_size > 0 ? value_size - 1 : 0);

				while (!internal_name.empty() && internal_name.back() == L'\0') {
					internal_name.pop_back();
				}

				return Field<std::wstring>::available(std::move(internal_name));
			}
		}

		return Field<std::wstring>::unavailable(ErrorInfo{
			"VerQueryValueW",
			0,
			"O executável não possui o campo InternalName."
			});
	}

	Field<CollectionResult<ModuleRecord>> QueryProcessModules(std::uint32_t pid) {
		CollectionResult<ModuleRecord> result;
		HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
		SnapshotHandle snapshot(h);
		if (!snapshot.is_valid()) {
			const DWORD error = GetLastError();
			result.state = CollectionState::failed;
			result.diagnostics.push_back(ErrorInfo{
				"CreateToolhelp32Snapshot",
				static_cast<std::uint32_t>(error),
				"Não foi possível criar o snapshot de módulos do processo."
				});
			return Field<CollectionResult<ModuleRecord>>::unavailable(result.diagnostics.back());
		}
		MODULEENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		if (!Module32FirstW(snapshot.get(), &entry)) {
			const DWORD error = GetLastError();
			if (error != ERROR_NO_MORE_FILES) {
				result.state = CollectionState::failed;
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
			result.items.push_back(ModuleRecord{
				entry.szModule,
				Field<std::wstring>::available(entry.szExePath),
				reinterpret_cast<std::uint64_t>(entry.modBaseAddr)
				});
		} while (Module32NextW(snapshot.get(), &entry));
		const DWORD error = GetLastError();
		if (error != ERROR_NO_MORE_FILES) {
			result.state = CollectionState::partial;
			result.diagnostics.push_back(ErrorInfo{
				"Module32NextW",
				static_cast<std::uint32_t>(error),
				"Não foi possível obter todos os módulos do processo."
				});
		}
		return Field<CollectionResult<ModuleRecord>>::available(result);
	}

	ProcessDetails QueryProcessDetails(std::uint32_t pid, const InspectionOptions& options) {
		HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
		ProcessHandle process_handle(h);
		ProcessDetails details;

		if (!process_handle.is_valid()) {
			const DWORD error = GetLastError();
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

		if (options.include_image_path) {
			std::wstring path(_MAX_PATH, L'\0');
			for (;;) {
				DWORD length = static_cast<DWORD>(path.size());
				if (QueryFullProcessImageNameW(process_handle.get(), 0, path.data(), &length)) {
					path.resize(length);
					details.image_path = Field<std::wstring>::available(std::move(path));
					break;
				}

				const DWORD error = GetLastError();
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
		if (GetProcessTimes(process_handle.get(), &creation_time, &exit_time, &kernel_time, &user_time)) {
			const std::uint64_t ticks =
				(static_cast<std::uint64_t>(creation_time.dwHighDateTime) << 32) |
				creation_time.dwLowDateTime;
			details.creation_time = Field<std::uint64_t>::available(ticks);
		}
		else {
			const DWORD error = GetLastError();
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
			details.modules = QueryProcessModules(pid);
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

	if (res.state == QueryState::failed) {		
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
	QueryResult res = QueryProcess(options);

	if (res.state == QueryState::failed) {
		return res;
	}

	std::erase_if(res.processes, [name](const ProcessRecord& process) {
		return CompareStringOrdinal(
			process.name.value()->c_str(), -1,
			name.c_str(), -1,
			TRUE) != CSTR_EQUAL;
		});

	return res;
}

QueryResult QueryProcessByFullPath(std::wstring full_path, const InspectionOptions& options) {
	QueryResult res = QueryProcess(InspectionOptions{
		true, // garante que o caminho da imagem seja incluído na consulta
		options.include_internal_name,
		options.include_signature,
		options.include_modules,
		options.max_concurrent_processes
		});

	if (res.state == QueryState::failed) {
		return res;
	}
	
	std::erase_if(res.processes, [full_path](const ProcessRecord& process) {
		if (process.image_path.state() != FieldState::available) {
			return true; // remove se não tiver caminho de imagem disponível
		}

		return CompareStringOrdinal(
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
	const std::size_t max_workers =
		(std::max)(std::size_t{ 1 }, options.max_concurrent_processes);

	// Guarda as análises que começaram, mas ainda não foram finalizadas.
	// O índice informa em qual processo o resultado deve ser colocado depois.
	std::vector<PendingInspection> pending;
	pending.reserve(max_workers);

	HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	SnapshotHandle snapshot(h);

	if (!snapshot.is_valid()) {
		const DWORD error = GetLastError();

		res.state = QueryState::failed;
		res.fatal_error = ErrorInfo{
			"CreateToolhelp32Snapshot",
			static_cast<std::uint32_t>(error),
			"Não foi possível criar o snapshot de processos."
		};

		return res;
	}

	PROCESSENTRY32W entry{};

	entry.dwSize = sizeof(entry);

	if (!Process32FirstW(snapshot.get(), &entry)) {
		const DWORD error = GetLastError();

		if (error == ERROR_NO_MORE_FILES) {
			return res; // ainda está complete e vazio
		}

		res.state = QueryState::failed;
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

		// Se um campo tiver uma mensagem de erro, adiciona essa mensagem ao resultado geral.
		auto add_field_diagnostic = [&res](const auto& field) {
			if (field.error().has_value()) {
				res.diagnostics.push_back(*field.error());
			}
		};

		if (options.include_image_path && process.image_path.state() != FieldState::available) {
			res.state = QueryState::partial;
			add_field_diagnostic(process.image_path);
		}
		if (options.include_signature && process.signature.state() != FieldState::available) {
			res.state = QueryState::partial;
			add_field_diagnostic(process.signature);
		}
		if (process.creation_time_100ns.state() != FieldState::available) {
			res.state = QueryState::partial;
			add_field_diagnostic(process.creation_time_100ns);
		}
		if (options.include_internal_name && process.internal_name.state() != FieldState::available) {
			res.state = QueryState::partial;
			add_field_diagnostic(process.internal_name);
		}
		if (options.include_modules) {
			if (process.modules.state() != FieldState::available) {
				res.state = QueryState::partial;
				add_field_diagnostic(process.modules);
			}
			else if (process.modules.value()->state != CollectionState::complete) {
				res.state = QueryState::partial;
				for (const ErrorInfo& error : process.modules.value()->diagnostics) {
					res.diagnostics.push_back(error);
				}
			}
		}
	};

	// Pega a análise mais antiga da lista e espera por ela se ainda estiver em andamento.
	// Depois, coloca o resultado no processo correspondente.
	auto collect_one = [&pending, &apply_details]() {
		PendingInspection inspection = std::move(pending.front());
		pending.erase(pending.begin());
		apply_details(inspection.process_index, inspection.result.get());
	};

	BOOL has_next = TRUE;
	do {
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
			// Inicia a análise em paralelo, sem esperar a análise anterior terminar.
			pending.push_back(PendingInspection{
				res.processes.size() - 1,
				entry.th32ProcessID,
				std::async(std::launch::async, InspectProcess, entry.th32ProcessID, options)
			});
			// Quando atingimos o limite, esperamos uma análise terminar antes de iniciar outra.
			if (pending.size() >= max_workers) {
				collect_one();
			}
		}
		has_next = Process32NextW(snapshot.get(), &entry);
	} while (has_next);

	// Terminamos de ler o snapshot; agora esperamos as análises que ainda faltam.
	while (!pending.empty()) {
		collect_one();
	}

	if (!has_next) {
		const DWORD error = GetLastError();
		if (error != ERROR_NO_MORE_FILES) {
			res.state = QueryState::partial;
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
				process.parent_name = it->name;
			}
			else {
				process.parent_name = Field<std::wstring>::unavailable(ErrorInfo{
					"Process32FirstW/Process32NextW",
					0,
					"Não foi possível encontrar o processo pai na lista de processos."
					});
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