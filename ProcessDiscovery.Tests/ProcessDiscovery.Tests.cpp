// ProcessDiscovery.Tests.cpp : Este arquivo contém a função 'main'. A execução do programa começa e termina ali.
//

#include "../ProcessDiscovery/ProcessCatalog.h"

#define NOMINMAX
#include <Windows.h>

#include <iostream>
#include <string>
#include <vector>
#include <map>

namespace {
	std::wstring GetCurrentImagePath() {
		std::vector<wchar_t> buffer(512);
		for (;;) {
			DWORD length = static_cast<DWORD>(buffer.size());
			if (QueryFullProcessImageNameW(GetCurrentProcess(), 0, buffer.data(), &length)) {
				return std::wstring(buffer.data(), length);
			}

			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= 32768) {
				return {};
			}

			buffer.resize((std::min)(buffer.size() * 2, static_cast<std::size_t>(32768)));
		}
	}

	bool ContainsPid(const QueryResult& result, std::uint32_t pid) {
		for (const ProcessRecord& process : result.processes) {
			if (process.pid == pid) {
				return true;
			}
		}
		return false;
	}
}

int main() {
	ProcessCatalog catalog;
	const std::uint32_t currentPid = static_cast<std::uint32_t>(GetCurrentProcessId());
	const std::wstring imagePath = GetCurrentImagePath();
	if (imagePath.empty()) {
		std::cerr << "FAIL: não foi possível obter o caminho do executável de teste.\n";
		return 1;
	}

	InspectionOptions noImagePath{};
	noImagePath.include_image_path = false;

	const QueryResult byPid = catalog.by_pid(currentPid, noImagePath);
	if (byPid.state == ResultState::failed || byPid.processes.size() != 1 ||
		byPid.processes.front().pid != currentPid ||
		byPid.processes.front().image_path.state() != FieldState::not_requested) {
		std::cerr << "FAIL: by_pid ou include_image_path=false.\n";
		return 1;
	}
	if (byPid.processes.front().ppid.state() != FieldState::available ||
		byPid.processes.front().parent_name.state() != FieldState::available ||
		byPid.processes.front().parent_name.value()->empty()) {
		std::cerr << "FAIL: parent_name não foi resolvido para o processo atual.\n";
		return 1;
	}
	if (byPid.processes.front().creation_time_100ns.state() != FieldState::available ||
		!byPid.processes.front().creation_time_100ns.value().has_value() ||
		byPid.processes.front().creation_time_100ns.value().value() == 0) {
		std::cerr << "FAIL: creation_time_100ns não foi obtido para o processo atual.\n";
		return 1;
	}
	if (byPid.processes.front().internal_name.state() != FieldState::not_requested) {
		std::cerr << "FAIL: internal_name deveria estar not_requested por padrão.\n";
		return 1;
	}
	if (byPid.processes.front().signature.state() != FieldState::not_requested) {
		std::cerr << "FAIL: signature deveria estar not_requested por padrão.\n";
		return 1;
	}

	if (byPid.processes.front().modules.state() != FieldState::not_requested) {
		std::cerr << "FAIL: modules deveria estar not_requested por padrão.\n";
		return 1;
	}

	const QueryResult byPidWithImage = catalog.by_pid(currentPid);
	if (byPidWithImage.state == ResultState::failed ||
		byPidWithImage.processes.size() != 1 ||
		byPidWithImage.processes.front().pid != currentPid ||
		byPidWithImage.processes.front().image_path.state() != FieldState::available ||
		byPidWithImage.processes.front().creation_time_100ns.state() != FieldState::available ||
		byPidWithImage.processes.front().parent_name.state() != FieldState::available) {
		std::cerr << "FAIL: by_pid não obteve os detalhes do processo atual.\n";
		return 1;
	}

	InspectionOptions withInternalName{};
	withInternalName.include_internal_name = true;
	const QueryResult byPidWithInternalName = catalog.by_pid(currentPid, withInternalName);
	if (byPidWithInternalName.state == ResultState::failed ||
		byPidWithInternalName.processes.size() != 1 ||
		(byPidWithInternalName.processes.front().internal_name.state() == FieldState::available &&
			(!byPidWithInternalName.processes.front().internal_name.value().has_value() ||
			 byPidWithInternalName.processes.front().internal_name.value()->empty())) ||
		(byPidWithInternalName.processes.front().internal_name.state() != FieldState::available &&
			byPidWithInternalName.processes.front().internal_name.state() != FieldState::unavailable &&
			byPidWithInternalName.processes.front().internal_name.state() != FieldState::access_denied)) {
		std::cerr << "FAIL: estado inconsistente em include_internal_name.\n";
		return 1;
	}
	if (byPidWithInternalName.processes.front().internal_name.state() == FieldState::unavailable) {
		std::cout << "INFO: executável de teste não possui metadado InternalName.\n";
	}

	const std::uint32_t missingPid = 0xFFFFFFFFu;
	const QueryResult missing = catalog.by_pid(missingPid, noImagePath);
	if (missing.state == ResultState::failed || !missing.processes.empty()) {
		std::cerr << "FAIL: by_pid para PID inexistente.\n";
		return 1;
	}

	std::wcout << L"PID: " << byPid.processes.front().pid << L", Executável: " << byPid.processes.front().image_path.value().value_or(L"<desconhecido>") << L"\n";
	std::wcout << L"Nome: " << byPid.processes.front().name.value().value_or(L"<desconhecido>") << L"\n";
	std::wcout << L"PPID: " << byPid.processes.front().ppid.value().value_or(0) << L"\n";
	std::wcout << L"Nome do Pai: " << byPid.processes.front().parent_name.value().value_or(L"<desconhecido>") << L"\n";
	std::wcout << L"Creation time (100ns desde 1601 UTC): "
		<< byPid.processes.front().creation_time_100ns.value().value() << L"\n";

	const std::size_t slash = imagePath.find_last_of(L"\\/");
	const std::wstring imageName = imagePath.substr(slash == std::wstring::npos ? 0 : slash + 1);
	std::wstring caseVariant = imageName;
	for (wchar_t& ch : caseVariant) {
		if (ch >= L'a' && ch <= L'z') {
			ch = static_cast<wchar_t>(ch - L'a' + L'A');
		}
		else if (ch >= L'A' && ch <= L'Z') {
			ch = static_cast<wchar_t>(ch - L'A' + L'a');
		}
	}

	const QueryResult byName = catalog.by_name(caseVariant, noImagePath);
	if (byName.state == ResultState::failed || !ContainsPid(byName, currentPid)) {
		std::cerr << "FAIL: by_name não encontrou o processo atual ignorando caixa.\n";
		return 1;
	}

	std::wcout << L"PID: " << byName.processes.front().pid << L", Executável: " << byName.processes.front().image_path.value().value_or(L"<desconhecido>") << L"\n";
	std::wcout << L"Nome: " << byName.processes.front().name.value().value_or(L"<desconhecido>") << L"\n";
	std::wcout << L"PPID: " << byName.processes.front().ppid.value().value_or(0) << L"\n";
	std::wcout << L"Nome do Pai: " << byName.processes.front().parent_name.value().value_or(L"<desconhecido>") << L"\n";

	const QueryResult byPath = catalog.by_full_path(imagePath, noImagePath);
	bool foundByPath = false;
	for (const ProcessRecord& process : byPath.processes) {
		if (process.pid == currentPid && process.image_path.state() == FieldState::available) {
			foundByPath = true;
		}
	}
	if (byPath.state == ResultState::failed || !foundByPath) {
		std::cerr << "FAIL: by_full_path não encontrou o processo atual.\n";
		return 1;
	}

	std::wcout << L"PID: " << byPath.processes.front().pid << L", Executável: " << byPath.processes.front().image_path.value().value_or(L"<desconhecido>") << L"\n";
	std::wcout << L"Nome: " << byPath.processes.front().name.value().value_or(L"<desconhecido>") << L"\n";
	std::wcout << L"PPID: " << byPath.processes.front().ppid.value().value_or(0) << L"\n";
	std::wcout << L"Nome do Pai: " << byPath.processes.front().parent_name.value().value_or(L"<desconhecido>") << L"\n";

	std::map<std::pair<std::string, std::uint32_t>, std::size_t> diagnosticsCount;

	for (const ErrorInfo& error : byPath.diagnostics) {
		std::pair<std::string, std::uint32_t> key;

		key.first = error.operation + ": " + error.message;
		key.second = error.native_code;

		diagnosticsCount[key]++;
	}

	std::cout << "INFO: diagnostics count:\n";
	for (const auto& [key, count] : diagnosticsCount) {
		const auto& [operation, native_code] = key;
		std::cout << "  Operation: " << operation << ", Native Code: " << native_code << ", Count: " << count << "\n";
	}

	const QueryResult all = catalog.enumerate(noImagePath);
	if (all.state == ResultState::failed || !ContainsPid(all, currentPid)) {
		std::cerr << "FAIL: enumerate não incluiu o processo atual.\n";
		return 1;
	}
	bool imageNotRequested = false;
	for (const ProcessRecord& process : all.processes) {
		if (process.pid == currentPid && process.image_path.state() == FieldState::not_requested) {
			imageNotRequested = true;
		}
	}
	if (!imageNotRequested) {
		std::cerr << "FAIL: enumerate não respeitou include_image_path=false.\n";
		return 1;
	}

	InspectionOptions allWithInternalName{};
	allWithInternalName.include_internal_name = true;
	const QueryResult allWithMetadata = catalog.enumerate(allWithInternalName);
	if (allWithMetadata.state == ResultState::failed || allWithMetadata.processes.empty()) {
		std::cerr << "FAIL: enumerate com include_internal_name não retornou processos.\n";
		return 1;
	}

	std::size_t internalNamesAvailable = 0;
	for (const ProcessRecord& process : allWithMetadata.processes) {
		if (process.internal_name.state() == FieldState::available &&
			process.internal_name.value().has_value() &&
			!process.internal_name.value()->empty()) {
			++internalNamesAvailable;
			std::wcout << L"INFO: InternalName encontrado - PID " << process.pid
				<< L": " << *process.internal_name.value() << L"\n";
		}
	}
	if (internalNamesAvailable == 0) {
		std::cout << "INFO: nenhum processo expôs metadado InternalName.\n";
	}
	else {
		std::cout << "PASS: processos com InternalName: " << internalNamesAvailable << "\n";
	}

	InspectionOptions allWithSignaturesOptions{};
	allWithSignaturesOptions.include_signature = true;
	const QueryResult allWithSignatures = catalog.enumerate(allWithSignaturesOptions);
	if (allWithSignatures.state == ResultState::failed || allWithSignatures.processes.empty()) {
		std::cerr << "FAIL: enumerate com include_signature não retornou processos.\n";
		return 1;
	}

	std::size_t signaturesPresent = 0;
	std::size_t signaturesAbsent = 0;
	std::size_t signaturesIndeterminate = 0;
	std::size_t signaturesUnavailable = 0;
	for (const ProcessRecord& process : allWithSignatures.processes) {
		if (process.signature.state() != FieldState::available ||
			!process.signature.value().has_value()) {
			++signaturesUnavailable;
			continue;
		}

		const SignatureStatus& signature = *process.signature.value();
		switch (signature.presence) {
		case SignaturePresence::present:
			++signaturesPresent;
			break;
		case SignaturePresence::no_embedded_signature:
			++signaturesAbsent;
			break;
		case SignaturePresence::unknown:
			++signaturesIndeterminate;
			break;
		}
	}

	std::cout << "INFO: assinaturas - presentes: " << signaturesPresent
		<< ", ausentes: " << signaturesAbsent
		<< ", indeterminadas: " << signaturesIndeterminate
		<< ", indisponíveis: " << signaturesUnavailable << "\n";

	InspectionOptions withModules{};
	withModules.include_modules = true;
	const QueryResult byPidWithModules = catalog.by_pid(currentPid, withModules);
	if (byPidWithModules.state == ResultState::failed ||
		byPidWithModules.processes.size() != 1 ||
		byPidWithModules.processes.front().modules.state() != FieldState::available ||
		!byPidWithModules.processes.front().modules.value().has_value()) {
		std::cerr << "FAIL: include_modules não retornou a coleção de módulos.\n";
		return 1;
	}

	const auto& modules = *byPidWithModules.processes.front().modules.value();
	std::wcout << L"INFO: módulos do processo atual: " << modules.items.size() << L"\n";
	if (modules.state == ResultState::failed) {
		std::cerr << "FAIL: coleta de módulos falhou.\n";
		return 1;
	}

	// Este cenário usa apenas um trabalhador. Assim, cada processo é analisado
	// antes que o próximo comece, simulando uma execução sequencial.
	InspectionOptions oneWorker{};
	oneWorker.include_image_path = false;
	const QueryResult oneWorkerResult = catalog.enumerate(oneWorker);
	if (oneWorkerResult.state == ResultState::failed ||
		!ContainsPid(oneWorkerResult, currentPid)) {
		std::cerr << "FAIL: enumerate com um worker.\n";
		return 1;
	}

	// Zero não é um limite útil. A biblioteca deve corrigir esse valor e usar
	// pelo menos um trabalhador, em vez de deixar a consulta sem executar.
	InspectionOptions zeroWorkers{};
	zeroWorkers.include_image_path = false;
	const QueryResult zeroWorkersResult = catalog.enumerate(zeroWorkers);
	if (zeroWorkersResult.state == ResultState::failed ||
		!ContainsPid(zeroWorkersResult, currentPid)) {
		std::cerr << "FAIL: enumerate com zero workers.\n";
		return 1;
	}

	// Quatro trabalhadores permitem que até quatro processos sejam analisados
	// ao mesmo tempo, que é o limite padrão da biblioteca.
	InspectionOptions severalWorkers{};
	severalWorkers.include_image_path = false;
	const QueryResult severalWorkersResult = catalog.enumerate(severalWorkers);
	if (severalWorkersResult.state == ResultState::failed ||
		!ContainsPid(severalWorkersResult, currentPid)) {
		std::cerr << "FAIL: enumerate com vários workers.\n";
		return 1;
	}

	InspectionOptions withSignature{};
	withSignature.include_signature = true;
	const QueryResult byPidWithSignature = catalog.by_pid(currentPid, withSignature);
	if (byPidWithSignature.state == ResultState::failed ||
		byPidWithSignature.processes.size() != 1 ||
		byPidWithSignature.processes.front().signature.state() != FieldState::available ||
		!byPidWithSignature.processes.front().signature.value().has_value()) {
		std::cerr << "FAIL: include_signature não retornou um status de assinatura.\n";
		return 1;
	}

	const SignatureStatus& signature = *byPidWithSignature.processes.front().signature.value();
	std::wcout << L"INFO: assinatura - presença: " << static_cast<int>(signature.presence)
		<< L", confiança: " << static_cast<int>(signature.trust)
		<< L", WinTrust: " << signature.wintrust_status << L"\n";

	// Combina todos os recursos opcionais em uma única consulta ao processo atual.
	// Isso verifica que os campos solicitados não permanecem como not_requested.
	InspectionOptions allOptionalFields{};
	allOptionalFields.include_image_path = false;
	allOptionalFields.include_internal_name = true;
	allOptionalFields.include_signature = true;
	allOptionalFields.include_modules = true;
	const QueryResult allOptionalFieldsResult = catalog.by_pid(currentPid, allOptionalFields);
	if (allOptionalFieldsResult.state == ResultState::failed ||
		allOptionalFieldsResult.processes.size() != 1 ||
		allOptionalFieldsResult.processes.front().image_path.state() != FieldState::not_requested ||
		allOptionalFieldsResult.processes.front().internal_name.state() == FieldState::not_requested ||
		allOptionalFieldsResult.processes.front().signature.state() == FieldState::not_requested ||
		allOptionalFieldsResult.processes.front().modules.state() == FieldState::not_requested) {
		std::cerr << "FAIL: combinação de opções opcionais não foi respeitada.\n";
		return 1;
	}

	const QueryResult absent = catalog.by_full_path(imagePath + L".not-found", noImagePath);
	if (absent.state == ResultState::failed || !absent.processes.empty()) {
		std::cerr << "FAIL: busca negativa retornou falha global ou correspondências.\n";
		return 1;
	}

	std::wcout << L"PASS: enumerate, by_pid, by_name e by_full_path para PID " << currentPid << L".\n";
	if (absent.state == ResultState::partial) {
		std::wcout << L"INFO: busca negativa inconclusiva (partial), diagnósticos: "
			<< absent.diagnostics.size() << L".\n";
	}
	else {
		std::wcout << L"PASS: caminho inexistente sem correspondências (complete).\n";
	}
	std::wcout << L"PASS: by_pid com e sem caminho e PID inexistente.\n";
	return 0;
}

// Executar programa: Ctrl + F5 ou Menu Depurar > Iniciar Sem Depuração
// Depurar programa: F5 ou menu Depurar > Iniciar Depuração

// Dicas para Começar: 
//   1. Use a janela do Gerenciador de Soluções para adicionar/gerenciar arquivos
//   2. Use a janela do Team Explorer para conectar-se ao controle do código-fonte
//   3. Use a janela de Saída para ver mensagens de saída do build e outras mensagens
//   4. Use a janela Lista de Erros para exibir erros
//   5. Ir Para o Projeto > Adicionar Novo Item para criar novos arquivos de código, ou Projeto > Adicionar Item Existente para adicionar arquivos de código existentes ao projeto
//   6. No futuro, para abrir este projeto novamente, vá para Arquivo > Abrir > Projeto e selecione o arquivo. sln
