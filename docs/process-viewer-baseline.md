# Baseline técnico — Visualizador de processos

**Tarefa:** T-01 / evidências T-03 / T-04 / T-05  
**Data do registro:** 2026-10-20  
**Status:** baseline confirmado; T-03, T-04 e T-05 concluídos para o escopo de tabela/árvore e shell WPF

## Decisões aprovadas

- UI: WPF com .NET 8.
- Builds: somente `x86` e `x64`; `AnyCPU` é proibido.
- Testes UI: FlaUI.UIA3 com xUnit.
- Validação JSON Schema: JsonSchema.Net, Draft 2020-12; validação semântica separada.
- Backend interno: assembly `ProcessDiscovery.Application`, namespace
  `ProcessDiscovery.Application.Backend`, contrato `Task<BackendResponse>
  QueryAsync(QueryRequest, CancellationToken)`.
- Integração nativa: somente bridge C ABI versionada; a ABI C++ original não é
  consumida pelo C#.
- Empacotamento: diretórios independentes para `x86` e `x64`.

## Ambiente detectado

- Visual Studio Community 2026, MSBuild 18.
- MSVC toolset `v145` (`14.51.36231`).
- Windows SDK `10.0`.
- DLL nativa: C++20, `DynamicLibrary`, CRT dinâmica (`/MDd` em Debug e
  `/MD` nas demais configurações), plataformas Win32/x64.
- SDK .NET detectado: `10.0.402`; runtime Windows Desktop x64 `10.0.13`.
- SDK .NET 8 instalado: `8.0.426`; SDK .NET 10.0.402 também permanece
  disponível. O alvo do projeto será fixado em `net8.0-windows`.

## VERSIONINFO e artefatos nativos

Os projetos nativos agora compilam um recurso `VERSIONINFO` determinístico,
versão `1.0.0.0`. A versão foi lida de `VS_FIXEDFILEINFO` após a build; não foi
usado fallback por timestamp, nome, manifesto ou hash.

| Arquitetura | Artefato | `file_version` | SHA-256 observado |
|---|---|---|---|
| x86 | `ProcessDiscovery.dll` | `1.0.0.0` | `c7019bc31b2e0fdf2c19927484059efd676380c930913a93dace35d1d37898e3` |
| x64 | `ProcessDiscovery.dll` | `1.0.0.0` | `7d8c3216469e77232fcfa17b2288eb6071b5146b0bdcc1e522e011c656db76ad` |
| x86 | `ProcessDiscovery.Bridge.dll` | `1.0.0.0` | `9c87bf1412bd4cdd0dcd9636e333e28857be1f3f155ede512556b116c380ee93` |
| x64 | `ProcessDiscovery.Bridge.dll` | `1.0.0.0` | `7ca62624cac18a8058cb394bb82e81263a50df598429d53acfba0d6e2270c05b` |

Os hashes acima foram recalculados nos pacotes locais após `/Brepro`; devem ser
regenerados quando o artefato for recompilado. Os manifestos x86/x64 foram
validados pelo `tools/ProcessDiscovery.ContractValidator`.

## Evidências T-03

- ABI/layout, ownership, argumentos inválidos, catalog create/free e smoke tests:
  executados em x86/x64 conforme disponibilidade dos pacotes.
- Cancelamento e `PD_BUSY`: o harness `PD_TESTING` usa gate determinístico antes
  da enumeração. Os pacotes isolados x86 e x64 passaram com `free_status=10`,
  resultado `PD_CANCELLED` e exit code 0.
- Unload: x64 validado após o harness passar a derivar o caminho absoluto da
  própria imagem; a mesma correção deve ser compilada para Win32.
- `PD_TESTING`: smoke isolado x86/x64 passou com `PD_SERIALIZATION_FAILED` e
  resultado nulo. A auditoria de exports confirmou que os hooks não existem nas
  DLLs de produção.
- `PD_RESULT_FAILED`: PID inválido não é caso válido; permanece pendente uma
  fixture/caminho público determinístico que produza esse status.
- Fixtures `complete`, `partial` e `failed` passaram schema Draft 2020-12 e
  semântica no validador .NET 8.

**Status T-03:** concluído para o contrato público exercitado e para a matriz
x86/x64 disponível. `PD_RESULT_FAILED` permanece sem caminho público determinístico
no conjunto atual; o teste de PID inválido não é contado como evidência desse
status. A lacuna é documentada, não mascarada.

## Evidências T-04

- Criados `src/ProcessDiscovery.Application` e
  `tests/ProcessDiscovery.Application.Tests`, ambos direcionados a
  `net8.0-windows` com plataformas explícitas `x86` e `x64`.
- Implementados `IProcessQueryBackend`, `QueryRequest`, `BackendResponse`,
  `ProcessSnapshot`, `ProcessRecord`, `FieldValue<T>` e diagnósticos, com
  `ResultState`, `FieldState` e `ObservationConsistency` tipados e invariantes
  para estados de campo e respostas fatais.
- Implementados parser decimal restrito a `uint32`, árvore por `pid`/`ppid`,
  raízes para pai ausente, proteção contra `ppid == pid`, mismatch e ciclos,
  avaliação de identidade por `creation_time_100ns` e preservação do snapshot
  anterior em resposta fatal.
- A árvore rejeita relações inconsistentes, ciclos e PID duplicado sem bloquear;
  o serviço valida modo/PID e preserva snapshot anterior em fatalidade.
- Suíte xUnit: 19 testes aprovados.
- Builds Release `PlatformTarget=x86` e `PlatformTarget=x64`: aprovados.

**Status T-04:** concluído para a camada de domínio/serviço abstrata.

**Limite:** a implementação não inclui a bridge C ABI concreta nem a UI WPF;
essas fronteiras permanecem para as etapas seguintes.

## Evidências T-05

- Criados modelos de apresentação para tabela, árvore, seleção e estado acessível
  em `src/ProcessDiscovery.Application/Presentation`.
- A tabela é derivada do snapshot e ordenada por PID crescente; a árvore usa
  `pid`/`ppid`, mantendo diagnósticos de pai não observado, ciclos e
  inconsistências produzidos pelo domínio.
- Estados `Completo`, `Parcial`, `Falha` e cancelamento são expostos como texto
  acessível, sem depender apenas de cor. A seleção só aceita PIDs presentes no
  snapshot corrente.
- `ProcessPresentationController` executa atualização assíncrona sobre
  `ProcessQueryService`, preserva o snapshot anterior em resposta fatal e
  atualiza o estado de busy/cancelamento.
- Suíte xUnit: 24 testes aprovados em execução Debug x64 e Release x64.
- Build Release x86 aprovado; execução x86 não foi possível porque o runtime
  `Microsoft.NETCore.App 8.0` x86 não está instalado no ambiente.

**Status T-05:** concluído para a camada de apresentação e shell WPF, incluindo
tabela, árvore, virtualização configurada, seleção e estados acessíveis.

## Shell visual WPF — evidências adicionais

- Criado `src/ProcessDiscovery.App/ProcessDiscovery.App.csproj`, executável
  `net8.0-windows`, com `UseWPF=true` e plataformas explícitas `x86`/`x64`.
- Criados `App.xaml`, `MainWindow.xaml` e `MainWindow.xaml.cs` com status
  acessível, `DataGrid` para a tabela e `TreeView` para a árvore.
- `DataGrid` está configurado com virtualização de linhas e colunas e modo
  `Recycling`; `TreeView` usa virtualização e reciclagem do painel quando
  suportadas pelo controle.
- A janela atualiza os controles a partir de `ProcessPresentationState`, usa o
  Dispatcher para chamadas fora da UI e protege os eventos contra reentrada.
- `App.OnStartup` cria e exibe `MainWindow`, mantendo o estado inicial acessível
  sem inserir dados fictícios.
- Testes estruturais verificam a presença dos controles, propriedades de
  virtualização, nomes de automação e inicialização da janela.
- Build Release do shell WPF: `x86` e `x64` aprovados.
- Testes da camada Application e do shell: 26 aprovados em Release x64.
- A execução dos testes x86 continua pendente por ausência do runtime
  `Microsoft.NETCore.App 8.0` x86 no ambiente; a build x86 foi aprovada.

**Limites atuais:** a janela ainda não recebe um backend concreto nem executa
consulta ao processo nativo; essa integração permanece fora do T-05. Não foram
executados testes interativos FlaUI/UIA3 nesta etapa. O runtime x86 ausente
impede executar a suíte x86, mas não impede a compilação do artefato x86.

## Evidências T-06 — módulos, assinatura e identidade

- O schema local `docs/contracts/process-viewer-result.schema.json` é versão 3
  e define `fieldModules`, `moduleCollection`, `module` e `fieldSignature`.
- A camada Application agora modela `InternalName`, `ModuleRecord`,
  `ModuleCollection`, `SignatureRecord` e seus estados normativos, preservando
  valores nulos nos estados que não permitem valor.
- `ProcessDetailPresentation` expõe nome interno, módulos com nome/caminho/
  endereço base, estado da coleção, diagnósticos, presença/confiança da
  assinatura, `wintrust_status`, `last_error` e política.
- Suíte xUnit: 31 testes aprovados em Release x64, incluindo coleção completa,
  coleção parcial, `not_requested`, erro e diagnósticos de assinatura.
- Builds Release do shell WPF em x64 e x86: aprovadas.

**Status T-06:** parcialmente concluído. Detalhe básico, identidade, DTOs e
apresentação de módulos/assinatura estão implementados. Permanece pendente o
desserializador/backend que materializa a resposta JSON v3 da bridge nos novos
DTOs; nenhuma informação foi sintetizada enquanto essa fronteira não estiver
conectada.

### Materialização JSON v3

- Criado `ProcessResultV3Deserializer`, com mapeamento explícito de
  `schema_version=3`, estados de resultado/campo, diagnósticos, campos de
  processo, `moduleCollection` e `signature`.
- Criado `JsonProcessQueryBackend`, que recebe uma função de obtenção do JSON,
  propaga cancelamento, desserializa a resposta e traduz `failed` em resposta
  fatal do serviço.
- Suíte xUnit: 35 testes aprovados em Release x64, incluindo materialização de
  módulos parciais, assinatura, estados sem valor, diagnósticos e schema
  incompatível.
- Builds Release do shell WPF em x64 e x86: aprovadas.

**Integração concluída:** o backend JSON está conectado ao loader/bridge concreto
por `NativeBridgeProcessQueryBackend`, sem dados fictícios e sem alteração da ABI.

### Adaptador nativo C ABI

- Criado `NativeBridgeProcessQueryBackend`, com carregamento dinâmico da DLL,
  resolução dos símbolos públicos v1, `NativeOptions` compatível com o layout
  x86/x64 e consultas por enumeração ou PID.
- O adaptador usa `SafeHandle` para catálogo, resultado e cancelamento; copia o
  JSON pela API `size/copy`, propaga cancelamento e entrega o texto ao
  `ProcessResultV3Deserializer`.
- Não houve alteração da ABI C nem uso da ABI C++ restrita.
- DLLs existentes verificadas em `x64/Release/ProcessDiscovery.Bridge.dll` e
  `Win32/Release/ProcessDiscovery.Bridge.dll`.
- A aplicação WPF resolve o pacote por arquitetura, executa a enumeração inicial
  e consulta detalhes por PID ao selecionar uma linha ou nó da árvore.
- A chamada nativa é executada fora da thread da interface e o cancelamento é
  propagado ao handle nativo.
- Suíte xUnit: 38 testes aprovados em Release x64; builds WPF Release x86 e
  x64 aprovadas.

**Evidência end-to-end:** o teste x64 executa uma consulta real por PID contra o
pacote canônico contendo `ProcessDiscovery.Bridge.dll`, `ProcessDiscovery.dll` e
`artifacts-manifest.json`, materializando snapshot, módulos, assinatura e
diagnósticos. As suítes x86 e x64 executam 38 testes aprovados; o x86 usa o
runtime .NET 8 x86 instalado no host.

### Empacotamento executável

- `scripts/Publish-ProcessDiscovery.ps1` publica a aplicação WPF framework-dependent
  e copia os artefatos nativos da arquitetura selecionada.
- Os pacotes finais são gerados em `dist/x86` e `dist/x64`, sem mistura de DLLs
  entre arquiteturas.
- Cada pacote contém `ProcessDiscovery.App.exe`, `ProcessDiscovery.Bridge.dll`,
  `ProcessDiscovery.dll` e `artifacts-manifest.json` no mesmo diretório.
- Os hashes SHA-256 dos manifestos foram conferidos contra as bridges publicadas
  para x86 (`9c87bf...ee93`) e x64 (`7ca626...c05b`).
- A inicialização da aplicação publicada foi validada fora do debugger para x86
  e x64; o processo permanece ativo após a abertura. Falhas de inicialização são
  exibidas em MessageBox e registradas em `%LOCALAPPDATA%\ProcessDiscovery\startup-error.log`.

## Layout de empacotamento

```text
dist/
  x86/
	ProcessDiscovery.App.exe
	ProcessDiscovery.Bridge.dll
	ProcessDiscovery.dll
	artifacts-manifest.json
  x64/
	ProcessDiscovery.App.exe
	ProcessDiscovery.Bridge.dll
	ProcessDiscovery.dll
	artifacts-manifest.json
```

Os pacotes são framework-dependent e exigem o .NET 8 Desktop Runtime da
arquitetura correspondente. Não há fallback entre arquiteturas.

## Protocolo de benchmark

1. Executar cada arquitetura separadamente.
2. Aquecer o processo e a bridge antes da medição.
3. Medir enumeração da fixture determinística de 2.000 registros e registrar
   p50, p95, máximo e memória.
4. Medir também o volume real observado pela enumeração nativa, sem alegar que
   o host possui 2.000 processos.
5. Registrar configuração, arquitetura, commit, artefato, número de iterações,
   cancelamento e falhas; não remover metas nem substituir falhas por médias.

Resultados numéricos serão anexados somente após a execução efetiva.
