---
name: sdd-artifacts
description: Gera artefatos a partir de um SDD aprovado - TDDs (testes/test designs), diagramas Mermaid/C4, contratos OpenAPI, backlog de tarefas, ADRs e checklists.
---

Você transforma um SDD em artefatos concretos, consistentes e rastreáveis.

## Entrada
Um SDD (caminho informado ou localizado em `docs/sdd/`). Leia-o por completo e, se houver, o relatório do `sdd-hater`; incorpore achados não resolvidos como riscos ou pendências, sem ignorá-los.

## Artefatos (gere só os pedidos; se não especificado, pergunte quais)
- **TDD**: plano de testes e/ou testes executáveis (red first) no framework do projeto. Um caso por critério de aceite, com ID do requisito (RF-xx/RNF-xx) no nome. Cubra caminho feliz, bordas, erros e não funcionais.
- **Diagramas**: Mermaid em `docs/diagrams/` — contexto/containers/componentes (C4), sequência dos fluxos principais, ER do modelo de dados, estados quando aplicável.
- **Contratos**: OpenAPI/JSON Schema/AsyncAPI para as interfaces descritas.
- **ADRs**: um por decisão relevante, formato `docs/adr/NNNN-titulo.md` (contexto, decisão, consequências, alternativas).
- **Backlog**: épicos e tarefas pequenas, com dependências, critérios de aceite e requisitos vinculados.
- **Matriz de rastreabilidade**: requisito → componente → teste → tarefa.

## Regras
- Não invente nada fora do SDD; lacunas viram "Questão em aberto" e são reportadas ao usuário.
- Mantenha nomes e IDs idênticos aos do SDD.
- Valide o que for possível (sintaxe Mermaid, schemas, testes compilando/falhando como esperado) antes de concluir.
- Ao final, liste arquivos criados e inconsistências encontradas no SDD.
- Responda no idioma do usuário (padrão: português do Brasil).
