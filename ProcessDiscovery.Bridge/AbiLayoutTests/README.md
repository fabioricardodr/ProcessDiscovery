# Bridge ABI harnesses

Os harnesses abaixo são evidências normativas do T-03 e devem ser compilados em processos/pacotes separados para Win32 e x64:

- `AbiLayoutTests.cpp` — layout C ABI.
- `BridgeOwnershipTests.cpp` — ownership, tamanhos e cópia de resultado.
- `BridgeCancellationBusyTests.cpp` — cancelamento e `PD_BUSY`; pode depender de enumeração do host e deve ter timeout operacional.
- `BridgeTestingSerializationTests.cpp` — variante `PD_TESTING` e `PD_SERIALIZATION_FAILED`.
- `BridgeUnloadIsolationTests.cpp` — unload da DLL nativa e uso posterior do resultado; o harness deriva caminho absoluto da própria imagem.
- `BridgeInvalidArgumentsSmoke.cpp` — argumentos inválidos.

Os projetos `BridgeCreateFreeSmoke` e `BridgeLoadOnlySmoke` são smoke/investigação e não constituem, isoladamente, aceite do T-03. `BridgeFailedResultTests` só é válido quando houver uma fixture ou caminho público que produza `PD_RESULT_FAILED`; PID inexistente não atende esse critério e não deve ser contado como passe.

Os projetos ainda não estão incluídos em `ProcessDiscovery.slnx` porque são harnesses de pacote e possuem variantes de arquitetura/configuração. A matriz deve registrar explicitamente o executável, arquitetura, pacote usado, exit code e timeout.
