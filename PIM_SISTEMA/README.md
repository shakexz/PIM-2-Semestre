
# ERP Torra — Segurança, Permissões e TI

Projeto acadêmico em C integrado ao PostgreSQL usando libpq.

## Funcionalidades
- Login com senha armazenada como hash usando pgcrypto/bcrypt.
- Verificação de usuário, senha, status da conta e status do funcionário.
- Bloqueio de funcionário desligado.
- Perfis e permissões.
- Cadastro de usuários (com validação de CPF, senha e perfil).
- Bloqueio e revogação de acesso.
- Reativação de usuário.
- Desligamento de funcionário.
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

Quem tem `USUARIOS_GERENCIAR` (perfil ADMIN) entra no menu de Seguranca/TI:

1. Listar usuarios
2. Cadastrar usuario
3. Bloquear usuario
4. Revogar acesso
5. Reativar usuario
6. Desligar funcionario (permissao exclusiva do ADMIN)
7. Consultar permissoes dos perfis
8. Consultar logs
9. Alterar minha senha
10. Recuperar senha de usuario

Os demais usuarios veem as opcoes de consulta de permissoes, vendas, estoque, compras, financeiro, relatorios/logs (GERENCIA) e alteracao da propria senha. O item 0 faz logoff, registra LOGOFF e retorna para a tela de login sem fechar o programa. O acesso as operacoes e controlado pelas permissoes do perfil.
## 7. Fluxo para demonstrar no PIM

### Teste 1 — Login
1. Entrar como `admin`.
2. Mostrar o menu de Segurança/TI.

### Teste 2 — Permissões
1. Cadastrar um usuário com o perfil ESTOQUE (a opção 2 já cria o funcionário e o usuário).
2. Sair e entrar com esse usuário.
3. Na opção 1 (Consultar minhas permissões), mostrar que ele possui `ESTOQUE_PRODUTOS_GERENCIAR` e `ESTOQUE_MINIMO_GERENCIAR`.
4. Mostrar que o módulo de Vendas e o Financeiro são negados.

### Teste 3 — Revogação
1. Entrar como admin e revogar (ou bloquear) o usuário.
2. Sair.
3. Tentar entrar novamente com o usuário revogado.
4. O sistema deve negar o acesso.

### Teste 4 — Desligamento
1. Como admin, usar a opção 6 (Desligar funcionário) e informar o ID do funcionário
   (o ID aparece na listagem de usuários, coluna `ID FUNC`).
2. Tentar entrar com esse usuário. Mesmo com a senha correta, ele não deverá entrar.

### Teste 5 — Logs
Entrar novamente como admin e consultar os logs para demonstrar:
- login realizado;
- login negado (com o login que foi tentado);
- bloqueio;
- revogação;
- cadastro de usuário;
- desligamento;
- troca de senha.

## Escopo atual

O projeto inclui telas operacionais para Vendas, Estoque, Compras, Financeiro, RH e Gestao/Relatorios, com autorizacao por permissao.

## Observação

O projeto é uma base acadêmica funcional para demonstração do módulo de Segurança/Permissões/TI. Para produção seriam necessários controles adicionais, como gestão segura de segredos, recuperação de senha, sessões, proteção contra tentativas repetidas de login, TLS e política de auditoria.

## Atualizacao do escopo e integracao entre grupos

O fluxo de cadastro de ferias foi retirado. O perfil ADMIN e o unico que recebe `FUNCIONARIOS_DESLIGAR`. VENDAS recebe `VENDAS_CADASTRAR`; ESTOQUE recebe `ESTOQUE_PRODUTOS_GERENCIAR` e `ESTOQUE_MINIMO_GERENCIAR`; COMPRAS recebe `COMPRAS_ALERTAS_VISUALIZAR`; GERENCIA recebe `RELATORIOS_VISUALIZAR` e `LOGS_VISUALIZAR`. Essas permissoes podem ser aplicadas a bancos ja existentes ao executar novamente `02_schema.sql`.

As telas operacionais usam tabelas PostgreSQL com prefixos por area: `estoque_produto`, `vendas_venda`, `compras_alerta_reposicao` e `compras_pedido`. Vendas baixa o saldo em uma operacao atomica; quando o saldo chega ao minimo, o sistema cria/atualiza um alerta aberto para Compras. O recebimento da compra atualiza o saldo e atende o alerta. Gestao tem relatorios mensais de vendas e compras, posicao de estoque e consulta de logs. As tabelas estao separadas por prefixo para facilitar a integracao futura com os demais grupos; alinhem com eles os campos e identificadores antes de consolidar o banco.

Para criar as tabelas operacionais e reaplicar a matriz de acessos em uma instalacao existente, execute novamente `02_schema.sql` no banco PostgreSQL `erp_torra`. O script remove concessoes antigas apenas dos perfis padrao e recria as permissões previstas; perfis personalizados nao sao alterados. Os menus mostram somente os modulos e operacoes liberados, e cada tela valida a permissao novamente ao abrir.

### Contrato de log para os grupos

`log_acesso` foi preparada para compartilhar eventos entre sistemas: `id_evento` (UUID), `sistema_origem`, `data_hora`, `actor_login`, `setor`, `tipo` e `descricao`. `id_usuario` e uma referencia local opcional; os demais grupos nao devem depender desse ID, pois ele muda de um banco para outro. Cada grupo deve usar a mesma convencao de campos e nomes de evento, com `sistema_origem` identificando a aplicacao. O login do ator e copiado para o evento para que o historico continue legivel mesmo sem o cadastro local daquele usuario.

### MongoDB no programa

O aplicativo envia os eventos novos para `erp_auditoria.audit_events` e consulta essa colecao na tela de logs. A conexao usa a variavel de ambiente `MONGODB_URI`; nao grave a URI nem a senha no codigo ou no Git. No MSYS2 UCRT64, o driver C pode ser instalado com `pacman -S mingw-w64-ucrt-x86_64-mongo-c-driver`. O script `compilar_e_rodar (1).bat` inclui as bibliotecas do driver na compilacao.

No Atlas, use um usuario de banco com permissao `readWrite` somente em `erp_auditoria`, configure a lista de IPs permitidos e obtenha a URI em Connect > Drivers > C. A aplicacao testa a conexao e cria um indice unico de `id_evento` ao iniciar. A colecao e criada pelo MongoDB no primeiro evento.

Os logs novos sao gravados no MongoDB. O historico PostgreSQL anterior foi limpo depois da confirmacao da gravacao e consulta no MongoDB; a tabela `log_acesso` continua no schema como referencia de compatibilidade.

Os grupos devem enviar documentos com `id_evento`, `sistema_origem`, `data_hora` (BSON Date), `actor_login`, `setor`, `tipo` e `descricao`. O `id_usuario` e opcional e local de cada sistema; nao use esse campo para relacionar usuarios entre bancos. Cada grupo precisa ter um usuario MongoDB proprio com acesso minimo necessario e a URI guardada como segredo.

O item `0 - Sair` faz logoff, registra `LOGOFF`, limpa os dados da sessao e retorna para a tela de login. O programa continua aberto para outro usuario entrar. Fechar o console ou uma queda de energia nao garante esse evento.
