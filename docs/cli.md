# ProcessDiscovery.Cli.exe

## 1. Pré-requisitos

O CLI é um executável Windows construído com o mesmo conjunto de compatibilidade da biblioteca. As configurações validadas, o toolset, o SDK, o padrão C++ e as arquiteturas suportadas estão descritos em [docs/abi.md](abi.md); este documento não repete a especificação de ABI.

O projeto do CLI usa Visual Studio Community 2026 / MSBuild 18 e:

- Visual Studio/MSBuild com Platform Toolset `v145`;
- Windows SDK `10.0`;
- C++20;
- arquiteturas Win32 e x64.

Após o build, o executável é gerado nos seguintes caminhos relativos à raiz do repositório:

| Configuração | Caminho |
|---|---|
| Debug \| x64 | `x64\Debug\ProcessDiscovery.Cli.exe` |
| Release \| x64 | `x64\Release\ProcessDiscovery.Cli.exe` |
| Debug \| Win32 | `Debug\ProcessDiscovery.Cli.exe` |
| Release \| Win32 | `Release\ProcessDiscovery.Cli.exe` |

## 2. Sintaxe formal

A forma geral, conforme a mensagem de ajuda implementada, é:

```text
ProcessDiscovery.Cli.exe <subcomando> [argumento] [flags]
```

Os subcomandos são:

| Subcomando | Argumento | Operação |
|---|---|---|
| `list` | nenhum | Enumera os processos observados. |
| `query-pid` | `<pid>` | Consulta o processo cujo PID é o valor numérico informado. O PID não pode ser negativo e deve caber em `uint32_t`. |
| `query-name` | `<nome>` | Enumera e filtra pelo nome do processo. A comparação implementada ignora diferenças entre maiúsculas e minúsculas. |
| `query-path` | `<caminho>` | Enumera e filtra pelo caminho completo da imagem. O caminho deve ser absoluto, UNC ou estendido (`\\?\...`). A comparação implementada ignora diferenças entre maiúsculas e minúsculas. |

Os argumentos que contêm espaços devem ser protegidos pelo mecanismo de quoting do shell. Argumentos adicionais, depois do argumento do subcomando, precisam ser flags reconhecidas.

O parser separa o argumento posicional das flags e aceita as duas ordens
documentadas. Cada subcomando de consulta exige exatamente um argumento.

### Flags

| Flag | Efeito implementado |
|---|---|
| `--modules` | Ativa `InspectionOptions::include_modules`. A inspeção tenta preencher `ProcessRecord::modules`. Falhas, bloqueios, cross-bitness, encerramento do processo ou enumeração incompleta podem tornar o resultado parcial. Os módulos não são impressos na saída do CLI. |
| `--signature` | Ativa `InspectionOptions::include_signature`. A inspeção tenta verificar a assinatura Authenticode usando o caminho da imagem. Sem um caminho de imagem disponível, o campo de assinatura fica indisponível e o resultado pode ser parcial. A assinatura não é impressa na saída do CLI. |
| `--internal-name` | Ativa `InspectionOptions::include_internal_name`. A inspeção tenta ler o nome interno do recurso de versão da imagem, quando o caminho da imagem está disponível. O valor não é impresso na saída do CLI. |
| `--no-image-path` | Desativa `InspectionOptions::include_image_path`. Para consultas comuns, o caminho da imagem deixa de ser solicitado. Se `--signature` ou `--internal-name` também for usado, essas inspeções dependem do caminho e podem ficar indisponíveis, produzindo resultado parcial. Em `query-path`, o código força a inclusão do caminho para realizar o filtro; portanto, nessa consulta específica, `--no-image-path` não elimina o caminho necessário ao filtro. |

Por padrão, `include_image_path` é verdadeiro; `include_modules`, `include_signature` e `include_internal_name` são falsos. Não há flag do CLI para alterar `allow_remote_file_inspection`; a invocação usa o valor padrão dessa opção.

## 3. Formatos de saída

O formato padrão é o TSV detalhado. A seleção explícita é feita com um valor
separado:

```text
--format tsv
--format json
--format legacy
```

`--format=json`, valores ausentes e formatos desconhecidos são inválidos e
retornam `1` sem consultar a DLL.

### TSV detalhado

O cabeçalho base é sempre:

```text
pid name name_state image_path image_path_state
```

As colunas opcionais são anexadas nesta ordem:

```text
internal_name internal_name_state internal_name_diagnostic
signature_presence signature_trust signature_wintrust_status signature_last_error signature_policy signature_state signature_diagnostic
modules_count modules_state modules_result_state modules_diagnostics
```

As colunas de módulos só aparecem com `--modules`. Estados usam os nomes
canônicos de `FieldState`; valores ausentes são `-`. Diagnósticos são objetos
JSON compactos e `modules_diagnostics` é uma lista JSON compacta. O resumo
global é escrito em UTF-8 no `stderr`, nunca misturado ao TSV de `stdout`.

### JSON

O JSON usa `schema_version=1` e o contrato em
`docs/contracts/cli-output.schema.json`. Valores ausentes são `null`. As
propriedades de módulos permanecem estáveis: sem `--modules`,
`modules=null`, `modules_count=null`, `modules_state=not_requested`,
`modules_result_state=null` e `modules_diagnostics=[]`. Com a flag, a coleção,
os estados e os diagnósticos são preservados na ordem observada.

Assinaturas preservam `presence`, `trust`, o `LONG` bruto de
`WinVerifyTrust`, `last_error` (inclusive `0`), a política
`WTD_UI_NONE;WTD_REVOKE_NONE;WTD_CACHE_ONLY_URL_RETRIEVAL`, o estado do campo e
o diagnóstico.

### Legacy

`--format legacy` mantém exatamente duas colunas (`pid` e `name`), sem
cabeçalho e com terminador `\n`. Não inclui estados, diagnósticos ou colunas
opcionais.

## 4. Exemplos de uso

Os exemplos abaixo mostram a forma suportada pelo CLI. Argumentos posicionais
podem aparecer antes ou depois das flags.

### `list`

```powershell
.\x64\Release\ProcessDiscovery.Cli.exe list
```

### `query-pid <pid>`

```powershell
.\x64\Release\ProcessDiscovery.Cli.exe query-pid 1234
```

O retorno depende do resultado da consulta; PID inválido retorna `1` sem
consultar a DLL.

### `query-name <nome>`

```powershell
.\x64\Release\ProcessDiscovery.Cli.exe query-name notepad.exe
```

O nome é comparado pela DLL sem distinção entre maiúsculas e minúsculas.

### `query-path <caminho>`

```powershell
.\x64\Release\ProcessDiscovery.Cli.exe query-path "C:\Windows\System32\notepad.exe"
```

O caminho deve ser absoluto, UNC ou estendido.

### Múltiplas flags

```powershell
.\x64\Release\ProcessDiscovery.Cli.exe list --modules --signature --internal-name
```

As flags adicionais alteram a inspeção e acrescentam as colunas correspondentes
ao TSV ou propriedades correspondentes ao JSON.

### Invocação inválida e retorno ao help

Um subcomando desconhecido faz o CLI imprimir a mesma mensagem de ajuda e retornar `1`:

```powershell
.\x64\Release\ProcessDiscovery.Cli.exe comando-inexistente
# saída: ProcessDiscovery, Uso: ..., Avisos: ...
# código de saída: 1
```

`--help` explicitamente solicitado imprime a ajuda e retorna `0`:

```powershell
.\x64\Release\ProcessDiscovery.Cli.exe --help
```

A mensagem de ajuda é exibida somente quando `--help` é solicitado; erros de
parsing são reportados como `parse_error` e retornam `1`.

## 5. Diagnósticos e estados

O TSV detalhado escreve uma linha por `ProcessRecord`, conforme as colunas
selecionadas. O formato legacy é o único que escreve exatamente duas colunas:

```text
<pid>\t<nome>\n
```

- primeira coluna: PID;
- separador: um caractere de tabulação (`U+0009`);
- segunda coluna: `ProcessRecord::name`;
- nome indisponível: `<indisponível>`;
- terminador de linha: `\n`.

Quando a saída é um console, `WriteText` usa `WriteConsoleW`, portanto escreve diretamente a representação wide do texto. Quando a saída não é um console, converte o texto para UTF-8 com `WideCharToMultiByte` e envia os bytes por `std::cout`. O código não adiciona BOM.

No TSV detalhado, estados e diagnósticos são serializados sem texto mágico.
No JSON, valores ausentes são `null` e diagnósticos globais permanecem no
envelope da consulta.

## 6. Códigos de retorno

A função `ResultCode` aplica as condições na ordem abaixo:

| Código | Condição exata |
|---:|---|
| `0` | Resultado completo com pelo menos um processo. |
| `1` | Parsing inválido, incluindo subcomando, argumento, flag ou formato inválido. Retornado antes da consulta. |
| `2` | O `QueryResult` está em `ResultState::failed`. Essa condição tem precedência sobre parcial/cancelamento no cálculo do código. |
| `3` | O `QueryResult` está completo, não houve cancelamento e `processes` está vazio. |
| `4` | O `QueryResult` está em `ResultState::partial` ou o handler de console registrou cancelamento. Essa condição é avaliada depois de `failed` e antes do caso de resultado vazio. |
| `5` | Falha de serialização ou escrita em `stdout`/`stderr`. Tem precedência sobre o estado da consulta. |

A solicitação explícita de `--help` retorna `0`; a ausência de argumentos imprime a ajuda, mas retorna `1`.

## 6. Comportamento de cancelamento

O CLI registra um handler com `SetConsoleCtrlHandler`. Para `CTRL_C_EVENT` (Ctrl+C), `CTRL_BREAK_EVENT` (Ctrl+Break) e `CTRL_CLOSE_EVENT` (fechamento do console), o handler define a flag atômica de cancelamento e retorna `TRUE`. Para outros eventos, retorna `FALSE`.

O `StopToken` usado na consulta lê essa flag. A enumeração verifica o token entre processos; a enumeração de módulos também o verifica. A consulta pode, portanto, preservar os itens já observados e marcar o `QueryResult` como `ResultState::partial`. Mesmo se a consulta terminar sem essa marcação, a presença da flag de cancelamento faz `ResultCode` retornar `4`, exceto quando o resultado está em `failed`, que retorna `2` por ter precedência.

## 7. Limitações e avisos operacionais

A mensagem produzida pelo próprio `--help` contém estes avisos, preservados sem reinterpretar o texto:

```text
Avisos: PPL, permissões, cross-bitness, volumes remotos, corridas de processo e bloqueios de módulos podem produzir resultado parcial.
no_embedded_signature significa que não foi encontrado um certificado embutido; não é falha global.
```

Esses avisos refletem limitações do ambiente e da inspeção. Em particular,
`no_embedded_signature` é um estado de presença da assinatura, não uma falha
global da consulta. O TSV imprime o resumo global no `stderr` e o JSON mantém
os diagnósticos no envelope.

## 8. Diagnóstico de resultados parciais

`QueryResult::state` pode ser `complete`, `partial` ou `failed`. `ResultState::partial` significa que a consulta retornou dados, mas alguma parte da enumeração ou inspeção não pôde ser confirmada ou concluída. No CLI, esse estado é exposto pelo código de saída `4`, pelo resumo TSV em `stderr` e pelos estados/diagnósticos dos processos.

Cada campo `Field<T>` de um `ProcessRecord` possui um `FieldState`:

| Estado | Interpretação definida pelo tipo |
|---|---|
| `available` | O valor está disponível. |
| `unavailable` | O valor não está disponível; pode haver `ErrorInfo` associado. |
| `not_requested` | A inspeção desse campo não foi solicitada. É o estado inicial dos campos opcionais quando a flag correspondente não foi usada; o caminho da imagem é solicitado por padrão, salvo `--no-image-path`. |
| `access_denied` | O acesso necessário foi negado; pode haver `ErrorInfo`. |
| `process_exited` | O processo terminou durante a operação; pode haver `ErrorInfo`. |
| `unsupported_architecture` | A operação não é suportada para a arquitetura observada; pode haver `ErrorInfo`. |
| `available_unverified` | Há um valor, mas ele não foi validado completamente; pode haver `ErrorInfo`. |
| `error` | A leitura falhou e o campo contém um `ErrorInfo`. |

Para módulos, além do `FieldState`, o `CollectionResult<ModuleRecord>` possui seu próprio `ResultState` e diagnósticos. Assim, uma coleção pode estar disponível como campo, mas ter estado `partial` internamente. A implementação também usa `available_unverified` quando a arquitetura não pôde ser comprovada ou quando a identidade do processo não pôde ser confirmada durante a enumeração.

A combinação de `ResultState::partial`, `FieldState` e `ErrorInfo` deve ser interpretada como diagnóstico de melhor esforço, não como garantia de que todos os campos foram obtidos. No executável atual, esses detalhes não aparecem nas colunas: somente PID, nome e o código de saída são observáveis diretamente.
