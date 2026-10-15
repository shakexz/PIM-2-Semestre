
# ERP Torra — Segurança, Permissões e TI

Projeto acadêmico em C integrado ao PostgreSQL usando libpq.

## Funcionalidades
- Login com senha armazenada como hash usando pgcrypto/bcrypt.
- Verificação de usuário, senha, status da conta e status do funcionário.
- Bloqueio de funcionário desligado.
- Bloqueio durante qualquer período de férias vigente, salvo férias canceladas ou encerradas.
- Perfis e permissões.
- Cadastro de usuários (com validação de CPF, senha e perfil).
- Bloqueio e revogação de acesso.
- Reativação de usuário.
- Desligamento de funcionário.
- Cadastro de férias.
- Consulta de permissões (as minhas e as de todos os perfis).
- Alteração da própria senha.
- Logs de acesso e eventos.
- Consulta de logs por período obrigatório, com usuário e setor opcionais.
- Redefinição de senha pelo administrador.

## Arquivos
- `01_criar_banco.sql` — cria o banco `erp_torra`.
- `02_schema.sql` — cria extensão, tabelas, relacionamentos, perfis, permissões e usuário admin.
- `main.c` — aplicação em C usando libpq.
- `compilar_e_rodar.bat` — compila e abre o programa no Windows (dois cliques).
- `output/` — pasta onde fica o `main.exe` gerado.
- `diagrama.jpg` — diagrama das tabelas em imagem.
- `diagrama.txt` — diagrama textual das tabelas.
- `README.md` — instruções.

## 1. Criar o banco

Execute no PostgreSQL:

```bash
psql -U postgres -f 01_criar_banco.sql
```

Depois execute o segundo arquivo conectado ao banco `erp_torra`:

```bash
psql -U postgres -d erp_torra -f 02_schema.sql
```

Se quiser começar do zero, apague o banco antes com `DROP DATABASE erp_torra;` e repita os dois passos.
O `02_schema.sql` pode ser executado mais de uma vez sem dar erro.

## 2. Configurar a conexão

No início do `main.c`, altere as linhas `#define DB_...` conforme o seu PostgreSQL:

```c
#define DB_HOST "localhost"
#define DB_PORT "5432"
#define DB_NAME "erp_torra"
#define DB_USER "postgres"
#define DB_PASSWORD "sua_senha_do_postgres"
```

Não coloque uma senha real no código em um projeto de produção. Para o PIM, ela serve apenas como configuração local de demonstração.

## 3. Compilar e rodar no Windows (jeito mais fácil)

Com o PostgreSQL instalado e o MSYS2 (UCRT64) com o `gcc`, dê **dois cliques** em
`compilar_e_rodar.bat`. Ele:

1. procura sozinho a `libpq` (do MSYS2 ou do PostgreSQL instalado em `C:\Program Files\PostgreSQL`);
2. compila o `main.c` para `output\main.exe`;
3. ajusta o PATH (para achar o `libpq.dll`) e já abre o programa.

No VS Code, o mesmo script roda com **Ctrl+Shift+B**.

Se aparecer `Falha na conexão`, confira o `DB_PASSWORD` (e as outras linhas `#define DB_...`)
no começo do `main.c` e se o serviço do PostgreSQL está ligado.

### Compilar manualmente (opcional)

```powershell
gcc main.c -o output\main.exe -I"C:/Program Files/PostgreSQL/18/include" -L"C:/Program Files/PostgreSQL/18/lib" -lpq
$env:PATH = 'C:\Program Files\PostgreSQL\18\bin;' + $env:PATH
.\output\main.exe
```

Use sempre o cabeçalho (`-I`) e a biblioteca (`-L`) da **mesma** origem: ou os dois do PostgreSQL
instalado, ou os dois do MSYS2 (`C:/msys64/ucrt64/include` e `C:/msys64/ucrt64/lib`).

Para rodar o `.exe` em outro computador, o `libpq.dll` (e as DLLs dele) precisa estar na mesma
pasta do `.exe` ou no PATH.

O programa já coloca o console em UTF-8 (`chcp 65001`) para os acentos aparecerem certo.

## 4. Compilar no Linux

Com `libpq` instalado:

```bash
gcc main.c -o erp_torra $(pg_config --cflags --libs)
./erp_torra
```

## 5. Login inicial

Usuário:
`admin`

Senha:
`admin123`

Para um projeto real, essa senha deve ser trocada. Use a opção **10 - Alterar minha senha** do menu.

## 6. Menus

Quem tem a permissão `USUARIOS_GERENCIAR` (perfil ADMIN) entra no menu de Segurança/TI:

1. Listar usuários
2. Cadastrar usuário
3. Bloquear usuário
4. Revogar acesso
5. Reativar usuário
6. Desligar funcionário
7. Cadastrar férias
8. Consultar permissões de todos os perfis
9. Consultar logs
10. Alterar minha senha
11. Recuperar senha de usuário

Os demais usuários entram na área do usuário:

1. Consultar minhas permissões
2. Acessar módulo de vendas
3. Acessar módulo de estoque
4. Acessar módulo de compras
5. Acessar financeiro
6. Cadastrar férias (só com a permissão `FERIAS_GERENCIAR`, perfil RH)
7. Consultar logs (só com a permissão `LOGS_VISUALIZAR`, perfil GERENCIA)
8. Alterar minha senha

## 7. Fluxo para demonstrar no PIM

### Teste 1 — Login
1. Entrar como `admin`.
2. Mostrar o menu de Segurança/TI.

### Teste 2 — Permissões
1. Cadastrar um usuário com o perfil ESTOQUE (a opção 2 já cria o funcionário e o usuário).
2. Sair e entrar com esse usuário.
3. Na opção 1 (Consultar minhas permissões), mostrar que ele possui `ESTOQUE_VISUALIZAR` e `ESTOQUE_EDITAR`.
4. Mostrar que o módulo de Vendas e o Financeiro são negados.

### Teste 3 — Revogação
1. Entrar como admin e revogar (ou bloquear) o usuário.
2. Sair.
3. Tentar entrar novamente com o usuário revogado.
4. O sistema deve negar o acesso.

### Teste 4 — Férias
1. Cadastrar um período de férias que inclua a data atual (opção 7).
2. Tentar acessar com o funcionário.
3. O login deve ser recusado durante o período.

### Teste 5 — Desligamento
1. Como admin, usar a opção 6 (Desligar funcionário) e informar o ID do funcionário
   (o ID aparece na listagem de usuários, coluna `ID FUNC`).
2. Tentar entrar com esse usuário. Mesmo com a senha correta, ele não deverá entrar.

### Teste 6 — Logs
Entrar novamente como admin e consultar os logs para demonstrar:
- login realizado;
- login negado (com o login que foi tentado);
- bloqueio;
- revogação;
- cadastro de usuário;
- desligamento;
- férias;
- troca de senha.

## Escopo atual

Este projeto cobre Segurança/TI, usuários, permissões, férias e logs. Os módulos de Vendas, Estoque, Compras e Financeiro são apenas opções demonstrativas de acesso; ainda não estão implementados nem integrados aos projetos dos outros grupos.

Os logs ficam no PostgreSQL junto com as demais tabelas e possuem setor, tipo, data/hora, usuário e descrição. A consulta exige data inicial e final e aceita login e setor como filtros opcionais. Exemplos de setor: RH e FINANCEIRO.

A recuperação de senha é feita pelo administrador, que define uma nova senha para um usuário ativo. O sistema registra essa ação no log. Não há envio automático de senha, pois o projeto não possui cadastro de e-mail ou outro canal de contato.

## Observação

O projeto é uma base acadêmica funcional para demonstração do módulo de Segurança/Permissões/TI. Para produção seriam necessários controles adicionais, como gestão segura de segredos, recuperação de senha, sessões, proteção contra tentativas repetidas de login, TLS e política de auditoria.
