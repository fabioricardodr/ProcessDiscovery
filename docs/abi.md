# ProcessDiscovery — estratégia de DLL e ABI

## Decisão
A biblioteca é distribuída como DLL. A ABI escolhida é **C++ restrita**, sem promessa de ABI universal.

Consumidores devem ser compilados com o mesmo toolset MSVC, arquitetura, configuração compatível e CRT dinâmica compatível com a DLL. A superfície atual expõe tipos C++ (`std::wstring`, `std::optional`, `std::vector` e classes de valor); portanto ela não é uma fronteira C estável.

## Configuração suportada

- Visual Studio Community 2026 / MSBuild 18.
- Platform Toolset `v145`.
- C++20 (`LanguageStandard=stdcpp20`).
- Windows SDK `10.0`.
- Arquiteturas Win32 e x64.
- Debug com `MultiThreadedDebugDLL` (`/MDd`).
- Release com `MultiThreadedDLL` (`/MD`).
- `ConfigurationType=DynamicLibrary`.

## Fronteira e ownership

- A DLL exporta a classe `ProcessCatalog` por `__declspec(dllexport)`/`__declspec(dllimport)`.
- Strings, vetores, opcionais, resultados e objetos retornados permanecem sob ownership do valor C++ retornado ao consumidor.
- Não há transferência de `HANDLE` ou outro recurso nativo pela API pública atual.
- Nenhum consumidor deve liberar memória retornada com `free`, `delete` ou uma CRT diferente.
- O consumidor deve destruir os valores C++ no mesmo ambiente de toolset/CRT compatível.

## Erros e exceções

- Erros operacionais são representados por `FieldState`, `ResultState`, `ErrorInfo`, `ErrorDomain` e códigos nativos preservados.
- `fatal_error` somente é válido quando o resultado está em `ResultState::failed`.
- Exceções C++ não fazem parte do contrato de interoperabilidade e não devem atravessar a fronteira da DLL. A implementação deverá converter falhas esperadas em resultados estruturados; falhas de alocação continuam uma limitação da ABI C++ restrita.

## Compatibilidade e versionamento

- Alterações em tipos públicos, enumerações, ordem/layout de estruturas, assinaturas ou toolset podem quebrar a ABI.
- Não há promessa de compatibilidade binária entre versões não recompiladas.
- Uma futura ABI C estável exigirá decisão separada, handles opacos, buffers e estruturas versionadas; ela não é introduzida nesta versão.

## Evidências

- `ProcessDiscovery/ProcessDiscovery.vcxproj` mantém `DynamicLibrary`, Win32/x64, `v145`, SDK `10.0`, C++20 e CRT dinâmica.
- `ProcessDiscovery.Tests` referencia a DLL por `ProjectReference` e funciona como consumidor mínimo compilado com o mesmo conjunto de configurações.
- A validação inclui a solução completa em `Debug|Win32`, `Release|Win32`, `Debug|x64` e `Release|x64`, com testes nativos, CLI e LoaderTest nas configurações aplicáveis.

## Evidências funcionais relacionadas

- `ProcessDiscovery.Tests/ProcessDiscovery.Tests.cpp` cobre estados de campos, consultas por PID/nome/caminho, VERSIONINFO, Authenticode, módulos, identidade, arquitetura e cancelamento determinístico por fake Win32.
- `ProcessDiscovery.Cli.exe --help` retorna código `0`; argumento inválido retorna `1`; consulta sem correspondências retorna `3`; resultado parcial retorna `4`.
- `ProcessDiscovery.LoaderTest.exe` valida carregamento, resolução de exportação decorada da classe C++ e liberação da DLL.

## Extensão desta entrega

`ProcessRecord` agora contém `std::vector<ErrorInfo> diagnostics`, preservando
diagnósticos associados ao processo além dos diagnósticos globais de
`QueryResult`. Essa alteração modifica o layout de um tipo C++ público
retornado por valor.

Consequentemente, `ProcessDiscovery.dll`, `ProcessDiscovery.Cli.exe`,
`ProcessDiscovery.Tests.exe`, `ProcessDiscovery.LoaderTest.exe` e qualquer
outro consumidor devem ser recompilados juntos, usando a mesma configuração,
arquitetura, toolset, CRT e versão dos headers. Não existe compatibilidade
binária entre uma DLL antiga e consumidores compilados após essa extensão.
Não foi introduzida uma ABI C nesta entrega.
