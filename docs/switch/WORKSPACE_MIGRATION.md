# Migração privada do workspace Dusklight/deko3d

## Escopo e estado

Este procedimento prepara **cópia privada, não publicação**. Nenhum push, mudança no
console, transferência para SD ou execução de GPU é autorizado pela migração.
O pacote copia `/home/gro/dusklight-deko3d` inteiro, inclusive `.git`, submódulos
inicializados, worktrees, fontes locais, alterações pendentes, builds ignorados e
logs/evidências históricos. Exclui somente `migration/`, onde ficam os próprios
pacotes e cópias de teste, para impedir inclusão recursiva.

Não é suficiente copiar somente a pasta do novo teste: os metadados dos worktrees
ficam em outros diretórios do workspace. O pacote preserva essa estrutura.
Repositórios shallow continuam shallow: o histórico local disponível é preservado,
mas não há alegação de histórico remoto completo ou de commits que nunca foram
baixados. Não executar atualização automática de submódulos ou dependências.

O código do teste foi commitado em `26bf5fa842d2a498b675fa53ed3455ee1ea1deef` e suas
evidências em `fc8588e103f265c3f54b9b7c57a87203c3596e29`. A documentação/ferramentas de
migração vêm em um commit posterior; consulte `package.json` para o commit exato
capturado no snapshot. Esse commit não é o código compilado no NRO.

Há alterações anteriores pendentes no Dawn, nos registros de hardware de Dusklight
e no Performance Monitor. **São preservadas sem aceite e sem modificar seus
checkouts originais.** A candidata do Dawn continua bloqueada. `pending-changes.json`
registra o estado; `pending-overlays/` fornece cópias adicionais dos patches staged,
unstaged e arquivos untracked. O snapshot completo já contém esses arquivos; não
aplique os overlays novamente sobre ele. O estado Git deve ser idêntico após a
restauração, incluindo a sujeira preexistente.

O pacote não copia `~/.hermes`, perfis/memórias, chaves SSH, configuração de login
Docker, credenciais do host, jobs agendados ou configuração de usuário. Não depende
da memória do assistente para identificar o estado técnico: use os registros em
`docs/switch/`. **Não é uma auditoria completa de segredos do histórico Git/logs.**
Trate o pacote como sensível e privado; não publique nem envie por link público.
Uma transferência entre máquinas deve usar canal privado com proteção adequada.

## Conteúdo entregue

- `workspace.tar.zst`: snapshot do workspace, incluindo metadados Git e artefatos.
- `toolchain.docker.tar.zst`: imagem Docker exata
  `sha256:6611f256a5f9c97ff9b934c7a7c0ec26552aee0fe94d4cd59d849def637e376c`.
- `files.json`: SHA-256/tamanho/modo de cada arquivo do workspace.
- `repositories.json`: HEAD, branch, status, submódulos e flags de índice de cada
  checkout encontrado, inclusive dependências de builds.
- `package.json`, `SHA256SUMS`: identidades dos pacotes e arquivos de entrega.
- `pending-changes.json`, `pending-overlays/`: alterações não aceitas preservadas.
- `restore_workspace.py`, testes e recibos do ensaio real de restauração.

A raiz do workspace não é um repositório Git. A documentação está commitada no
worktree `dependencies/deko3d-stencil-hardware-test`, branch
`research/stencil-z24s8-hardware-prep`. O snapshot leva os objetos/refs locais dos
outros repositórios sem depender de GitHub, remotes locais externos ou alternates.
Remotes locais que apontavam para a raiz antiga são relocados somente na cópia.

## Na máquina de destino — Linux x86_64 / WSL2 com Docker

Instale Python3.11+, Git, zstd e Docker funcional. Preserve o pacote original até o
fim da conferência. Reserve pelo menos80GB livres para arquivos comprimidos,
extração, imagem Docker, verificações e um build novo. A imagem é Linux/amd64;
outras arquiteturas/ambientes não foram validados por este procedimento.

Os comandos abaixo são **offline** e não contatam o console. Execute dentro da
pasta que recebeu todos os arquivos do pacote:

```sh
sha256sum -c SHA256SUMS
zstd -d workspace.tar.zst -o workspace.restore.tar
python3 restore_workspace.py extract workspace.restore.tar /caminho/novo/ensaio
python3 restore_workspace.py verify /caminho/novo/ensaio/dusklight-deko3d files.json
python3 restore_workspace.py relocate /caminho/novo/ensaio/dusklight-deko3d repositories.json
python3 restore_workspace.py audit-git /caminho/novo/ensaio/dusklight-deko3d repositories.json
zstd -dc toolchain.docker.tar.zst | docker load
docker image inspect sha256:6611f256a5f9c97ff9b934c7a7c0ec26552aee0fe94d4cd59d849def637e376c
```

O destino de `extract` **não pode existir**. O script recusa substituições, links,
nomes inseguros e arquivos especiais no tar. Verifique todos os hashes **antes**
de `relocate`. A relocação altera só metadados Git da cópia e mantém backups em
`.migration-metadata-originals/`. Ela não reescreve fontes, evidências, CMakeCache,
binários ou logs. O diretório original exato é recusado. A checagem Git é feita com
hooks/fsmonitor/contato remoto desabilitados e sem refresh do índice.

Os recibos antigos contêm caminhos absolutos como proveniência. Eles devem continuar
inalterados. Builds antigos com CMakeCache/caminhos absolutos não devem ser retomados
no novo host: use diretório isolado novo e a imagem fixada. O método testado monta o
workspace restaurado em `/home/gro/dusklight-deko3d` **dentro do container**, mesmo que
seu caminho no host seja outro. Não depende da existência do usuário `gro` no host.

## Retomar o teste mínimo sem hardware

`Z24S8_GPU_PREPARATION.md` é a referência para o contrato/limites do teste. O NRO
aceito para uma futura autorização é:

`builds/z24s8-hardware-preparation/switch-committed/z24s8_stencil_probe.nro`

SHA-256: `c734a74dd67d540cd59b61eaccc89eaac53c20bd318caf0b953865242764e7cd`.

O recibo `restored-build-result.json` descreve o build executado na cópia restaurada,
com seu comando e comparação de bytes. Para repetir no destino, adapte somente o
caminho host do workspace no comando; mantenha o caminho interno do container,
commit do código e hash da biblioteca fixos. Escolha outro diretório de saída,
recusando qualquer destino já existente. Não executar scripts de deploy/FTP/nxlink,
Dockerfile/scripts de limpeza ou a NRO durante a migração.

O checkpoint é **PREPARATION PASS**, não hardware validado. Sanitizer startup
DEADLYSIGNAL/timeout e a revisão inicialmente reprovada permanecem como evidência
negativa. A candidata de textura do Dawn continua bloqueada; SetImmediates/integração
continuam parciais onde os registros assim dizem. Um novo assistente deve ler os
registros antes de mudar o código e pedir autorização explícita para hardware.

## Verificação e conservação

O ensaio de migração deve comprovar: todos os arquivos listados conferem antes da
relocação; HEAD/branch/status/submódulos/flags de índice conferem depois; o novo
build, sem acesso ao workspace original, gera a mesma NRO. Consulte os recibos
entregues para contagens, hashes e exceções reais, não apenas este procedimento.
A presença de arquivos preservados não transforma resultados negativos em PASS.

Não apague a origem nem os pacotes após apenas copiar. Confira na outra máquina e
faça backup independente primeiro. Nenhuma limpeza destrutiva foi autorizada.
