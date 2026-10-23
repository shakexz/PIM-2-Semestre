#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libpq-fe.h>
#include <mongoc/mongoc.h>
#include <time.h>

#define DB_HOST "localhost"
#define DB_PORT "5432"
#define DB_NAME "erp_torra"
#define DB_USER "postgres"
#define DB_PASSWORD "vr300games"

PGconn *conn = NULL;
mongoc_client_t *mongo_client = NULL;
mongoc_collection_t *log_collection = NULL;
int usuario_logado = 0;
char login_logado[100];

/*
 * essa parte é só para limpar a tela
 * e deixar o programa mais bonito
 */
void limpar_tela(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

/*
 * Essa função é só para o programa parar um pouco e esperar o usuário
 * apertar Enter
 */
void pausa(void) {
    printf("\nPressione ENTER para continuar...");
    getchar();
}

/*
 * Aqui eu leio uma string do teclado. O programa pede uma mensagem e
 * guarda o valor em um vetor de caracteres. Isso é usado para login,
 * nome e outras entradas simples.
 * Se a pessoa digitar mais letras do que cabe no vetor, o que sobrou
 * é descartado para não atrapalhar a próxima pergunta.
 */
void ler_string(const char *msg, char *dest, size_t tam) {
    printf("%s", msg);
    if (fgets(dest, (int)tam, stdin)) {
        if (strchr(dest, '\n') == NULL) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
            }
        }
        dest[strcspn(dest, "\n")] = '\0';
    } else {
        dest[0] = '\0';
    }
}

int ler_inteiro(const char *msg, int *valor) {
    char buffer[32];
    ler_string(msg, buffer, sizeof(buffer));

    char *fim = NULL;
    long numero = strtol(buffer, &fim, 10);

    if (fim == buffer || *fim != '\0') {
        return 0;
    }

    *valor = (int)numero;
    return 1;
}

/*
 * Transforma o texto em letras maiúsculas. Serve para o usuário poder
 * digitar o perfil como "vendas" ou "VENDAS" e dar na mesma.
 */
void maiusculas(char *texto) {
    for (int i = 0; texto[i] != '\0'; i++) {
        texto[i] = (char)toupper((unsigned char)texto[i]);
    }
}

/*
 * Confere se o texto tem exatamente 11 caracteres e se todos são números.
 * É a validação simples do CPF (não calcula o dígito verificador).
 */
int cpf_valido(const char *cpf) {
    if (strlen(cpf) != 11) {
        return 0;
    }
    for (int i = 0; i < 11; i++) {
        if (!isdigit((unsigned char)cpf[i])) {
            return 0;
        }
    }
    return 1;
}

/*
 * Essa função responde uma pergunta simples: o usuário atual tem essa
 * permissão ou não? Se a consulta retornar alguma linha, significa que sim.
 * Também confere se o usuário ainda está ATIVO, assim, se o admin revogar
 * alguém que já está logado, a pessoa perde o acesso na hora.
 */
int tem_permissao(const char *permissao) {
    const char *params[2] = {login_logado, permissao};
    const char *sql =
        "SELECT 1 "
        "FROM usuario u "
        "JOIN usuario_perfil up ON up.id_usuario = u.id_usuario "
        "JOIN perfil_permissao pp ON pp.id_perfil = up.id_perfil "
        "JOIN permissao p ON p.id_permissao = pp.id_permissao "
        "WHERE u.login = $1 AND p.nome = $2 AND u.status = 'ATIVO' LIMIT 1";

    PGresult *res = PQexecParams(conn, sql, 2, NULL, params, NULL, NULL, 0);
    int ok = PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0;
    PQclear(res);
    return ok;
}

static int tem_alguma_permissao(const char *const permissoes[], size_t total) {
    for (size_t i = 0; i < total; i++)
        if (tem_permissao(permissoes[i])) return 1;
    return 0;
}

/* Conecta ao banco central de auditoria. A URI fica fora do codigo-fonte. */
int conectar_mongodb(void) {
    const char *uri_env = getenv("MONGODB_URI");
    bson_error_t error;
    bson_t comando = BSON_INITIALIZER;
    bson_t resposta = BSON_INITIALIZER;
    bson_t chaves = BSON_INITIALIZER;
    bson_t opcoes = BSON_INITIALIZER;
    bson_t resposta_indice = BSON_INITIALIZER;
    mongoc_index_model_t *modelo_indice = NULL;
    mongoc_index_model_t *modelos[1];
    int ok = 0;

    mongoc_init();
    if (uri_env == NULL || uri_env[0] == '\0') {
        fprintf(stderr, "Defina a variavel de ambiente MONGODB_URI antes de iniciar.\n");
        return 0;
    }

    mongo_client = mongoc_client_new(uri_env);
    if (mongo_client == NULL) {
        fprintf(stderr, "Nao foi possivel criar o cliente MongoDB. Confira MONGODB_URI.\n");
        goto cleanup;
    }

    BSON_APPEND_INT32(&comando, "ping", 1);
    if (!mongoc_client_command_simple(mongo_client, "admin", &comando, NULL,
                                      &resposta, &error)) {
        fprintf(stderr, "Falha ao conectar ao MongoDB: %s\n", error.message);
        goto cleanup;
    }

    log_collection = mongoc_client_get_collection(mongo_client,
                                                   "erp_auditoria",
                                                   "audit_events");
    if (log_collection == NULL) {
        fprintf(stderr, "Nao foi possivel abrir erp_auditoria.audit_events.\n");
        goto cleanup;
    }

    BSON_APPEND_INT32(&chaves, "id_evento", 1);
    BSON_APPEND_BOOL(&opcoes, "unique", true);
    BSON_APPEND_UTF8(&opcoes, "name", "uq_audit_events_id_evento");
    modelo_indice = mongoc_index_model_new(&chaves, &opcoes);
    if (modelo_indice == NULL) {
        fprintf(stderr, "Nao foi possivel preparar o indice de auditoria.\n");
        goto cleanup;
    }
    modelos[0] = modelo_indice;
    if (!mongoc_collection_create_indexes_with_opts(log_collection, modelos, 1,
                                                     NULL, &resposta_indice,
                                                     &error)) {
        fprintf(stderr, "Falha ao preparar indice id_evento no MongoDB: %s\n",
                error.message);
        goto cleanup;
    }
    ok = 1;

cleanup:
    if (modelo_indice != NULL) mongoc_index_model_destroy(modelo_indice);
    bson_destroy(&resposta_indice);
    bson_destroy(&opcoes);
    bson_destroy(&chaves);
    bson_destroy(&resposta);
    bson_destroy(&comando);
    return ok;
}

void desconectar_mongodb(void) {
    if (log_collection != NULL) {
        mongoc_collection_destroy(log_collection);
        log_collection = NULL;
    }
    if (mongo_client != NULL) {
        mongoc_client_destroy(mongo_client);
        mongo_client = NULL;
    }
    mongoc_cleanup();
}

/* Guarda os eventos novos diretamente na colecao central de auditoria. */
void registrar_log_com_ator(const char *setor, const char *tipo,
                            const char *descricao, const char *ator) {
    bson_t documento = BSON_INITIALIZER;
    bson_t resposta = BSON_INITIALIZER;
    bson_oid_t oid;
    char id_evento[25];
    bson_error_t error;
    int inserido;

    if (log_collection == NULL) {
        fprintf(stderr, "Aviso: MongoDB indisponivel; evento nao registrado.\n");
        bson_destroy(&resposta);
        bson_destroy(&documento);
        return;
    }

    bson_oid_init(&oid, NULL);
    bson_oid_to_string(&oid, id_evento);
    BSON_APPEND_UTF8(&documento, "id_evento", id_evento);
    BSON_APPEND_UTF8(&documento, "sistema_origem", "ERP_TORRA");
    BSON_APPEND_DATE_TIME(&documento, "data_hora", (int64_t)time(NULL) * 1000);
    if (usuario_logado > 0)
        BSON_APPEND_INT32(&documento, "id_usuario", usuario_logado);
    if (ator != NULL && ator[0] != '\0')
        BSON_APPEND_UTF8(&documento, "actor_login", ator);
    BSON_APPEND_UTF8(&documento, "setor", setor);
    BSON_APPEND_UTF8(&documento, "tipo", tipo);
    BSON_APPEND_UTF8(&documento, "descricao", descricao != NULL ? descricao : "");

    inserido = mongoc_collection_insert_one(log_collection, &documento, NULL,
                                            &resposta, &error);
    if (!inserido)
        fprintf(stderr, "Aviso: falha ao registrar log no MongoDB: %s\n", error.message);
    bson_destroy(&resposta);
    bson_destroy(&documento);
}

void registrar_log(const char *setor, const char *tipo, const char *descricao) {
    registrar_log_com_ator(setor, tipo, descricao, login_logado);
}

/*
 * Aqui a gente abre a conexão com o PostgreSQL. Sem essa parte, o programa
 * nem consegue conversar com o banco. É a base de tudo.
 */
int conectar(void) {
    char conninfo[256];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s dbname=%s user=%s password=%s",
             DB_HOST, DB_PORT, DB_NAME, DB_USER, DB_PASSWORD);

    conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Falha na conexão: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        conn = NULL;
        return 0;
    }

    /* Avisa o banco que o texto está em UTF8, para os acentos não quebrarem. */
    PQsetClientEncoding(conn, "UTF8");
    return 1;
}

/*
 * login do usuário. Ele digita o login e a senha, e o sistema confere se está correto. Se estiver, guarda o ID e o login do usuário logado.
 */
int login(void) {
    char usuario[100], senha[100];
    ler_string("Usuário: ", usuario, sizeof(usuario));
    ler_string("Senha: ", senha, sizeof(senha));

    const char *params[2] = {usuario, senha};

    const char *sql =
        "SELECT u.id_usuario, u.login "
        "FROM usuario u "
        "JOIN funcionario f ON f.id_funcionario = u.id_funcionario "
        "WHERE u.login = $1 "
        "AND u.senha_hash = crypt($2, u.senha_hash) "
        "AND u.status = 'ATIVO' "
        "AND f.status = 'ATIVO' "
        "";

    PGresult *res = PQexecParams(conn, sql, 2, NULL, params, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Erro no login: %s\n", PQerrorMessage(conn));
        PQclear(res);
        return 0;
    }

    if (PQntuples(res) == 0) {
        PQclear(res);

        /* Guarda no log qual login foi tentado (a senha nunca é guardada). */
        char descricao[200];
        snprintf(descricao, sizeof(descricao),
                 "Login negado (credenciais inválidas ou acesso restrito) para o login: %s",
                 usuario);

        registrar_log_com_ator("SEGURANCA", "LOGIN_NEGADO", descricao, usuario);

        printf("\nAcesso negado. Verifique usuário, senha e situação da conta/funcionário.\n");
        return 0;
    }

    usuario_logado = atoi(PQgetvalue(res, 0, 0));
    strncpy(login_logado, PQgetvalue(res, 0, 1), sizeof(login_logado) - 1);
    login_logado[sizeof(login_logado) - 1] = '\0';
    PQclear(res);

    const char *params_update[1] = {login_logado};
    const char *update_sql =
        "UPDATE usuario SET ultimo_acesso = CURRENT_TIMESTAMP WHERE login = $1";
    PGresult *upd = PQexecParams(conn, update_sql, 1, NULL, params_update, NULL, NULL, 0);
    PQclear(upd);

    registrar_log("SEGURANCA", "LOGIN_SUCESSO", "Login realizado com sucesso");
    return 1;
}

/*
 * Aqui o usuário troca a própria senha. 
 * A nova senha também é guardada com hash, igual no cadastro.
 */
void alterar_senha(void) {
    char atual[100], nova[100], confirmacao[100];
    ler_string("Senha atual: ", atual, sizeof(atual));
    ler_string("Nova senha (mínimo 4 caracteres): ", nova, sizeof(nova));
    ler_string("Repita a nova senha: ", confirmacao, sizeof(confirmacao));

    if (strlen(nova) < 4) {
        printf("A nova senha precisa ter pelo menos 4 caracteres.\n");
        return;
    }
    if (strcmp(nova, confirmacao) != 0) {
        printf("As senhas digitadas não são iguais.\n");
        return;
    }

    const char *params[3] = {nova, login_logado, atual};
    const char *sql =
        "UPDATE usuario SET senha_hash = crypt($1, gen_salt('bf')) "
        "WHERE login = $2 AND senha_hash = crypt($3, senha_hash)";

    PGresult *res = PQexecParams(conn, sql, 3, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) == PGRES_COMMAND_OK && atoi(PQcmdTuples(res)) > 0) {
        printf("Senha alterada com sucesso.\n");
        registrar_log("SEGURANCA", "SENHA_ALTERADA", "Usuário alterou a própria senha");
    } else {
        printf("Não foi possível alterar. Confira a senha atual.\n");
    }
    PQclear(res);
}

/* Recuperação simples para este projeto: o administrador define uma nova senha. */
void redefinir_senha_usuario(void) {
    char login[100], nova[100], confirmacao[100];
    ler_string("Login do usuário: ", login, sizeof(login));
    ler_string("Nova senha (mínimo 4 caracteres): ", nova, sizeof(nova));
    ler_string("Repita a nova senha: ", confirmacao, sizeof(confirmacao));
    if (strlen(nova) < 4 || strcmp(nova, confirmacao) != 0) {
        printf("Senha inválida ou as senhas não são iguais.\n");
        return;
    }
    const char *params[2] = {nova, login};
    const char *sql = "UPDATE usuario SET senha_hash=crypt($1, gen_salt('bf')) WHERE login=$2 AND status='ATIVO'";
    PGresult *res = PQexecParams(conn, sql, 2, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) == PGRES_COMMAND_OK && atoi(PQcmdTuples(res)) > 0) {
        char descricao[180];
        snprintf(descricao, sizeof(descricao), "Administrador redefiniu a senha do login %s", login);
        registrar_log("SEGURANCA", "SENHA_REDEFINIDA", descricao);
        printf("Senha redefinida. Avise o usuário por um canal combinado.\n");
    } else {
        printf("Usuário ativo não encontrado ou erro ao redefinir senha.\n");
    }
    PQclear(res);
}

/*
 * Lista os usuários em uma tabela simples para facilitar a leitura.
 * Mostra também o ID do funcionário, que é usado para cadastrar férias
 * e desligar funcionário.
 */
void listar_usuarios(void) {
    const char *sql =
        "SELECT u.id_usuario, f.id_funcionario, u.login, f.nome, f.setor, "
        "u.status, f.status "
        "FROM usuario u JOIN funcionario f ON f.id_funcionario = u.id_funcionario "
        "ORDER BY u.id_usuario";

    PGresult *res = PQexec(conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "%s\n", PQerrorMessage(conn));
        PQclear(res);
        return;
    }

    printf("\n%-8s %-8s %-16s %-26s %-12s %-10s %-12s\n",
           "ID USER", "ID FUNC", "LOGIN", "FUNCIONARIO", "SETOR", "USUARIO", "FUNCIONARIO");
    printf("--------------------------------------------------------------------------------------------\n");

    for (int i = 0; i < PQntuples(res); i++) {
        printf("%-8s %-8s %-16s %-26s %-12s %-10s %-12s\n",
               PQgetvalue(res, i, 0), PQgetvalue(res, i, 1),
               PQgetvalue(res, i, 2), PQgetvalue(res, i, 3),
               PQgetvalue(res, i, 4), PQgetvalue(res, i, 5),
               PQgetvalue(res, i, 6));
    }
    PQclear(res);
}

/*
 * Aqui eu revogo o acesso de um usuário. 
 */
void revogar_usuario(void) {
    int id = -1;
    char id_str[32];

    if (!ler_inteiro("ID do usuário para revogar: ", &id)) {
        printf("Informe um número válido para o ID do usuário.\n");
        return;
    }

    if (id == usuario_logado) {
        printf("Você não pode revogar o seu próprio acesso.\n");
        return;
    }

    snprintf(id_str, sizeof(id_str), "%d", id);

    const char *params[1] = {id_str};
    const char *sql = "UPDATE usuario SET status='REVOGADO' WHERE id_usuario=$1::integer";
    PGresult *res = PQexecParams(conn, sql, 1, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) == PGRES_COMMAND_OK && atoi(PQcmdTuples(res)) > 0) {
        printf("Acesso revogado.\n");
        char descricao[100];
        snprintf(descricao, sizeof(descricao), "Administrador revogou o acesso do usuário ID %d", id);
        registrar_log("SEGURANCA", "REVOGACAO", descricao);
    } else if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        printf("Usuário não encontrado. Confira o ID.\n");
    } else {
        printf("Erro: %s\n", PQerrorMessage(conn));
    }
    PQclear(res);
}

/*
 * Bloquear é parecido com revogar, mas esse status é quando a pessoa
 * fica temporariamente sem acesso.
 */
void bloquear_usuario(void) {
    int id = -1;
    char id_str[32];

    if (!ler_inteiro("ID do usuário para bloquear: ", &id)) {
        printf("Informe um número válido para o ID do usuário.\n");
        return;
    }

    if (id == usuario_logado) {
        printf("Você não pode bloquear o seu próprio acesso.\n");
        return;
    }

    snprintf(id_str, sizeof(id_str), "%d", id);

    const char *params[1] = {id_str};
    const char *sql = "UPDATE usuario SET status='BLOQUEADO' WHERE id_usuario=$1::integer";
    PGresult *res = PQexecParams(conn, sql, 1, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) == PGRES_COMMAND_OK && atoi(PQcmdTuples(res)) > 0) {
        printf("Usuário bloqueado.\n");
        char descricao[100];
        snprintf(descricao, sizeof(descricao), "Administrador bloqueou o usuário ID %d", id);
        registrar_log("SEGURANCA", "BLOQUEIO", descricao);
    } else if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        printf("Usuário não encontrado. Confira o ID.\n");
    } else {
        printf("Erro: %s\n", PQerrorMessage(conn));
    }
    PQclear(res);
}

/*
 * Essa função reativa o usuário quando ele foi bloqueado ou revogado.
 * Antes de ativar, a gente verifica se o funcionário ainda está ativo.
 */
void desbloquear_usuario(void) {
    int id = -1;
    char id_str[32];

    if (!ler_inteiro("ID do usuário para ativar: ", &id)) {
        printf("Informe um número válido para o ID do usuário.\n");
        return;
    }

    snprintf(id_str, sizeof(id_str), "%d", id);

    const char *params[1] = {id_str};
    const char *sql =
        "UPDATE usuario u SET status='ATIVO' "
        "WHERE u.id_usuario=$1::integer "
        "AND EXISTS (SELECT 1 FROM funcionario f WHERE f.id_funcionario=u.id_funcionario AND f.status='ATIVO')";

    PGresult *res = PQexecParams(conn, sql, 1, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) == PGRES_COMMAND_OK && atoi(PQcmdTuples(res)) > 0) {
        printf("Usuário ativado.\n");
        char descricao[100];
        snprintf(descricao, sizeof(descricao), "Administrador ativou o usuário ID %d", id);
        registrar_log("SEGURANCA", "DESBLOQUEIO", descricao);
    } else {
        printf("Não foi possível ativar. Verifique se o ID existe e se o funcionário está ativo.\n");
    }
    PQclear(res);
}

/*
 * Aqui a gente cadastra o funcionário e o usuário ao mesmo tempo. Primeiro
 * confere se os dados estão certos (nada vazio, CPF com 11 números, senha
 * com pelo menos 4 caracteres e perfil que existe). Depois cria o
 * funcionário, o login e por fim associa o perfil escolhido.
 */
void cadastrar_usuario(void) {
    char nome[100], cpf[20], setor[50], login_novo[100], senha[100], perfil_nome[50];
    ler_string("Nome: ", nome, sizeof(nome));
    ler_string("CPF (11 dígitos, só números): ", cpf, sizeof(cpf));
    ler_string("Setor: ", setor, sizeof(setor));
    ler_string("Login: ", login_novo, sizeof(login_novo));
    ler_string("Senha (mínimo 4 caracteres): ", senha, sizeof(senha));
    ler_string("Perfil (VENDAS/ESTOQUE/COMPRAS/FINANCEIRO/RH/GERENCIA): ",
                perfil_nome, sizeof(perfil_nome));

    maiusculas(perfil_nome);

    if (nome[0] == '\0' || setor[0] == '\0' || login_novo[0] == '\0') {
        printf("Nome, setor e login não podem ficar vazios.\n");
        return;
    }
    if (!cpf_valido(cpf)) {
        printf("CPF inválido. Digite exatamente 11 números, sem ponto ou traço.\n");
        return;
    }
    if (strlen(senha) < 4) {
        printf("A senha precisa ter pelo menos 4 caracteres.\n");
        return;
    }

    /* Confere se o perfil existe ANTES de cadastrar qualquer coisa. */
    const char *params_perfil[1] = {perfil_nome};
    PGresult *resp = PQexecParams(conn, "SELECT 1 FROM perfil WHERE nome = $1",
                                  1, NULL, params_perfil, NULL, NULL, 0);
    int perfil_existe = PQresultStatus(resp) == PGRES_TUPLES_OK && PQntuples(resp) > 0;
    PQclear(resp);

    if (!perfil_existe) {
        printf("Perfil inválido. Use um dos perfis listados.\n");
        return;
    }

    const char *params[6] = {nome, cpf, setor, login_novo, senha, perfil_nome};

    const char *sql =
        "WITH f AS ("
        " INSERT INTO funcionario(nome,cpf,setor,data_admissao) "
        " VALUES($1,$2,$3,CURRENT_DATE) RETURNING id_funcionario"
        "), u AS ("
        " INSERT INTO usuario(id_funcionario,login,senha_hash) "
        " SELECT id_funcionario,$4,crypt($5,gen_salt('bf')) FROM f "
        " RETURNING id_usuario"
        ") "
        "INSERT INTO usuario_perfil(id_usuario,id_perfil) "
        "SELECT u.id_usuario,p.id_perfil FROM u "
        "JOIN perfil p ON p.nome=$6";

    PGresult *res = PQexecParams(conn, sql, 6, NULL, params, NULL, NULL, 0);

    if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        printf("Usuário cadastrado com sucesso.\n");
        char descricao[200];
        snprintf(descricao, sizeof(descricao), "Novo usuário cadastrado: %s (perfil %s)",
                 login_novo, perfil_nome);
        registrar_log("SEGURANCA", "CADASTRO_USUARIO", descricao);
    } else {
        printf("Erro ao cadastrar (talvez o CPF ou o login já existam): %s\n",
               PQerrorMessage(conn));
    }
    PQclear(res);
}

/*
 * Aqui o funcionário é desligado da empresa. O status vira DESLIGADO e a
 * data de desligamento é a de hoje. Depois disso o login dele fica
 * bloqueado
 */
void desligar_funcionario(void) {
    int id = -1;
    char id_str[32], logado_str[32];

    if (!ler_inteiro("ID do funcionário para desligar: ", &id)) {
        printf("Informe um número válido para o ID do funcionário.\n");
        return;
    }

    snprintf(id_str, sizeof(id_str), "%d", id);
    snprintf(logado_str, sizeof(logado_str), "%d", usuario_logado);

    const char *params[2] = {id_str, logado_str};
    const char *sql =
        "UPDATE funcionario "
        "SET status='DESLIGADO', data_desligamento=CURRENT_DATE "
        "WHERE id_funcionario=$1::integer "
        "AND status <> 'DESLIGADO' "
        "AND id_funcionario <> (SELECT id_funcionario FROM usuario WHERE id_usuario=$2::integer)";

    PGresult *res = PQexecParams(conn, sql, 2, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) == PGRES_COMMAND_OK && atoi(PQcmdTuples(res)) > 0) {
        printf("Funcionário desligado.\n");
        char descricao[100];
        snprintf(descricao, sizeof(descricao), "Funcionário ID %d foi desligado", id);
        registrar_log("RH", "DESLIGAMENTO", descricao);
    } else if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        printf("Não foi possível desligar (ID não existe, já está desligado ou é o seu próprio).\n");
    } else {
        printf("Erro: %s\n", PQerrorMessage(conn));
    }
    PQclear(res);
}


void listar_permissoes(void) {
    const char *sql =
        "SELECT p.nome, pe.nome "
        "FROM perfil p JOIN perfil_permissao pp ON pp.id_perfil=p.id_perfil "
        "JOIN permissao pe ON pe.id_permissao=pp.id_permissao "
        "ORDER BY p.nome, pe.nome";

    PGresult *res = PQexec(conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "%s\n", PQerrorMessage(conn));
        PQclear(res);
        return;
    }

    printf("\n%-20s | %s\n", "PERFIL", "PERMISSAO");
    printf("-----------------------------------------------\n");
    for (int i = 0; i < PQntuples(res); i++) {
        printf("%-20s | %s\n", PQgetvalue(res, i, 0), PQgetvalue(res, i, 1));
    }

    PQclear(res);
}

/*
 * Parecida com a de cima, mas mostra só as permissões do usuário que
 * está logado (as dos perfis dele). É a que o usuário comum enxerga.
 */
void minhas_permissoes(void) {
    const char *params[1] = {login_logado};
    const char *sql =
        "SELECT p.nome, pe.nome "
        "FROM usuario u "
        "JOIN usuario_perfil up ON up.id_usuario = u.id_usuario "
        "JOIN perfil p ON p.id_perfil = up.id_perfil "
        "JOIN perfil_permissao pp ON pp.id_perfil = p.id_perfil "
        "JOIN permissao pe ON pe.id_permissao = pp.id_permissao "
        "WHERE u.login = $1 "
        "ORDER BY p.nome, pe.nome";

    PGresult *res = PQexecParams(conn, sql, 1, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "%s\n", PQerrorMessage(conn));
        PQclear(res);
        return;
    }

    printf("\nPermissões do usuário %s:\n", login_logado);
    printf("\n%-20s | %s\n", "PERFIL", "PERMISSAO");
    printf("-----------------------------------------------\n");
    for (int i = 0; i < PQntuples(res); i++) {
        printf("%-20s | %s\n", PQgetvalue(res, i, 0), PQgetvalue(res, i, 1));
    }
    if (PQntuples(res) == 0) {
        printf("(nenhuma permissão)\n");
    }

    PQclear(res);
}

/*
 * Essa função mostra os logs do sistema em ordem de data mais recente.
 * A ideia é manter um histórico do que aconteceu para auditoria.
 */
static int data_local_para_epoch(const char *texto, int dia_seguinte, int64_t *ms) {
    int ano, mes, dia;
    struct tm data = {0};
    struct tm conferida;
    time_t instante;
    char extra;
    if (strlen(texto) != 10 || sscanf(texto, "%2d/%2d/%4d%c", &dia, &mes, &ano, &extra) != 3 || ano < 1900 || mes < 1 || mes > 12 || dia < 1 || dia > 31) return 0;
    data.tm_year = ano - 1900; data.tm_mon = mes - 1; data.tm_mday = dia; data.tm_isdst = -1;
    instante = mktime(&data);
    if (instante == (time_t)-1) return 0;
    if (data.tm_year != ano - 1900 || data.tm_mon != mes - 1 || data.tm_mday != dia) return 0;
    if (dia_seguinte) {
        data.tm_mday++;
        data.tm_isdst = -1;
        instante = mktime(&data);
        if (instante == (time_t)-1) return 0;
    }
#ifdef _WIN32
    if (localtime_s(&conferida, &instante) != 0) return 0;
#else
    if (localtime_r(&instante, &conferida) == NULL) return 0;
#endif
    *ms = (int64_t)instante * 1000;
    return 1;
}

static const char *campo_texto(const bson_t *documento, const char *campo) {
    bson_iter_t iter;
    if (bson_iter_init_find(&iter, documento, campo) && BSON_ITER_HOLDS_UTF8(&iter)) return bson_iter_utf8(&iter, NULL);
    return "";
}

void listar_logs(void) {
    char inicio[16], fim[16], usuario[100], setor[51];
    int64_t inicio_ms, fim_ms;
    bson_t filtro = BSON_INITIALIZER, faixa = BSON_INITIALIZER;
    bson_t opcoes = BSON_INITIALIZER, ordem = BSON_INITIALIZER;
    bson_error_t error;
    mongoc_cursor_t *cursor;
    const bson_t *documento;
    int total = 0;
    ler_string("Data inicial (DD/MM/AAAA): ", inicio, sizeof(inicio));
    ler_string("Data final (DD/MM/AAAA): ", fim, sizeof(fim));
    ler_string("Login (Enter para todos): ", usuario, sizeof(usuario));
    ler_string("Setor (Enter para todos, ex.: RH, FINANCEIRO): ", setor, sizeof(setor));
    if (!data_local_para_epoch(inicio, 0, &inicio_ms) || !data_local_para_epoch(fim, 1, &fim_ms) || fim_ms <= inicio_ms) {
        printf("Informe datas validas no formato DD/MM/AAAA e em ordem.\n");
        return;
    }
    BSON_APPEND_DATE_TIME(&faixa, "$gte", inicio_ms);
    BSON_APPEND_DATE_TIME(&faixa, "$lt", fim_ms);
    BSON_APPEND_DOCUMENT(&filtro, "data_hora", &faixa);
    if (usuario[0] != '\0') BSON_APPEND_UTF8(&filtro, "actor_login", usuario);
    if (setor[0] != '\0') { maiusculas(setor); BSON_APPEND_UTF8(&filtro, "setor", setor); }
    BSON_APPEND_INT32(&ordem, "data_hora", -1);
    BSON_APPEND_DOCUMENT(&opcoes, "sort", &ordem);
    BSON_APPEND_INT64(&opcoes, "limit", 500);
    cursor = mongoc_collection_find_with_opts(log_collection, &filtro, &opcoes, NULL);
    printf("\nDATA/HORA            LOGIN              SETOR           TIPO              DESCRICAO\n");
    printf("------------------------------------------------------------------------------------------\n");
    while (mongoc_cursor_next(cursor, &documento)) {
        bson_iter_t iter;
        char data_formatada[24] = "-";
        if (bson_iter_init_find(&iter, documento, "data_hora") && BSON_ITER_HOLDS_DATE_TIME(&iter)) {
            int64_t epoch_ms = bson_iter_date_time(&iter);
            time_t segundos = (time_t)(epoch_ms / 1000);
            struct tm utc;
#ifdef _WIN32
            if (gmtime_s(&utc, &segundos) == 0)
#else
            if (gmtime_r(&segundos, &utc) != NULL)
#endif
                strftime(data_formatada, sizeof(data_formatada), "%Y-%m-%d %H:%M:%S", &utc);
        }
        const char *ator = campo_texto(documento, "actor_login");
        printf("%-20s %-18s %-15s %-17s %s\n", data_formatada, ator[0] ? ator : "-",
               campo_texto(documento, "setor"), campo_texto(documento, "tipo"), campo_texto(documento, "descricao"));
        total++;
    }
    if (mongoc_cursor_error(cursor, &error)) fprintf(stderr, "Falha ao consultar logs no MongoDB: %s\n", error.message);
    else if (total == 0) printf("Nenhum log encontrado no periodo informado.\n");
    mongoc_cursor_destroy(cursor);
    bson_destroy(&ordem); bson_destroy(&opcoes); bson_destroy(&faixa); bson_destroy(&filtro);
}

static int executar_atualizacao(const char *sql, int n, const char *const *params) {
    PGresult *res = PQexecParams(conn, sql, n, NULL, params, NULL, NULL, 0);
    int ok = PQresultStatus(res) == PGRES_TUPLES_OK || PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) printf("Falha na operacao: %s\n", PQerrorMessage(conn));
    PQclear(res);
    return ok;
}

static void listar_produtos(void) {
    PGresult *res = PQexec(conn,
        "SELECT id_produto,codigo,nome,quantidade,estoque_minimo "
        "FROM estoque_produto WHERE ativo ORDER BY nome");
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        printf("Falha ao consultar produtos: %s\n", PQerrorMessage(conn));
    } else {
        printf("\nID  CODIGO              PRODUTO                                  SALDO       MINIMO\n");
        for (int i = 0; i < PQntuples(res); i++)
            printf("%-3s %-20s %-40s %-11s %s\n", PQgetvalue(res,i,0), PQgetvalue(res,i,1),
                   PQgetvalue(res,i,2), PQgetvalue(res,i,3), PQgetvalue(res,i,4));
        if (PQntuples(res) == 0) printf("Nenhum produto cadastrado.\n");
    }
    PQclear(res);
}

static void atualizar_alerta_estoque(const char *id_produto) {
    const char *params[1] = {id_produto};
    executar_atualizacao(
        "INSERT INTO compras_alerta_reposicao(id_produto,quantidade_atual,estoque_minimo) "
        "SELECT id_produto,quantidade,estoque_minimo FROM estoque_produto "
        "WHERE id_produto=$1::bigint AND ativo AND quantidade <= estoque_minimo "
        "ON CONFLICT (id_produto) WHERE status='ABERTO' "
        "DO UPDATE SET quantidade_atual=EXCLUDED.quantidade_atual,estoque_minimo=EXCLUDED.estoque_minimo",
        1, params);
}

static void modulo_estoque(void) {
    const char *const permissoes[] = {"ESTOQUE_VISUALIZAR", "ESTOQUE_PRODUTOS_GERENCIAR", "ESTOQUE_MINIMO_GERENCIAR"};
    if (!tem_alguma_permissao(permissoes, sizeof(permissoes) / sizeof(permissoes[0]))) {
        puts("Acesso negado ao modulo de Estoque.");
        return;
    }
    int op = -1;
    do {
        printf("\n========== ESTOQUE ==========\n");
        if (tem_permissao("ESTOQUE_PRODUTOS_GERENCIAR")) printf("1 - Cadastrar produto\n3 - Registrar entrada de estoque\n");
        if (tem_permissao("ESTOQUE_VISUALIZAR")) printf("2 - Listar produtos e saldos\n");
        if (tem_permissao("ESTOQUE_MINIMO_GERENCIAR")) printf("4 - Definir estoque minimo\n");
        printf("0 - Voltar\n");
        if (!ler_inteiro("Escolha: ", &op)) { op = -1; continue; }
        if (op == 1) {
            char codigo[41], nome[121];
            if (!tem_permissao("ESTOQUE_PRODUTOS_GERENCIAR")) { puts("Acesso negado."); continue; }
            ler_string("Codigo do produto: ", codigo, sizeof(codigo));
            ler_string("Nome: ", nome, sizeof(nome));
            if (!codigo[0] || !nome[0]) { puts("Codigo e nome sao obrigatorios."); continue; }
            const char *p[2] = {codigo,nome};
            if (executar_atualizacao("INSERT INTO estoque_produto(codigo,nome) VALUES($1,$2)",2,p)) {
                registrar_log("ESTOQUE","PRODUTO_CADASTRADO",codigo); puts("Produto cadastrado.");
            }
        } else if (op == 2) {
            if (!tem_permissao("ESTOQUE_VISUALIZAR")) puts("Acesso negado."); else listar_produtos();
        } else if (op == 3 || op == 4) {
            int id; char idtxt[24], quantidade[32];
            const char *perm = op == 3 ? "ESTOQUE_PRODUTOS_GERENCIAR" : "ESTOQUE_MINIMO_GERENCIAR";
            if (!tem_permissao(perm)) { puts("Acesso negado."); continue; }
            if (!ler_inteiro("ID do produto: ",&id) || id <= 0) { puts("ID invalido."); continue; }
            snprintf(idtxt,sizeof(idtxt),"%d",id);
            ler_string(op == 3 ? "Quantidade recebida: " : "Novo estoque minimo: ",quantidade,sizeof(quantidade));
            char *fim=NULL; double q=strtod(quantidade,&fim);
            if (fim==quantidade || *fim || q < 0 || (op == 3 && q == 0)) { puts("Quantidade invalida."); continue; }
            const char *p[2] = {quantidade,idtxt};
            const char *sql = op == 3
                ? "UPDATE estoque_produto SET quantidade=quantidade+$1::numeric WHERE id_produto=$2::bigint AND ativo"
                : "UPDATE estoque_produto SET estoque_minimo=$1::numeric WHERE id_produto=$2::bigint AND ativo";
            if (executar_atualizacao(sql,2,p)) {
                atualizar_alerta_estoque(idtxt);
                registrar_log("ESTOQUE",op == 3 ? "ENTRADA_ESTOQUE" : "ESTOQUE_MINIMO_ALTERADO",idtxt);
                puts("Estoque atualizado.");
            }
        } else if (op != 0) puts("Opcao invalida.");
    } while (op != 0);
}

static void modulo_vendas(void) {
    const char *const permissoes[] = {"VENDAS_VISUALIZAR", "VENDAS_CADASTRAR"};
    if (!tem_alguma_permissao(permissoes, sizeof(permissoes) / sizeof(permissoes[0]))) {
        puts("Acesso negado ao modulo de Vendas.");
        return;
    }
    int op=-1;
    do {
        printf("\n========== VENDAS ==========\n");
        if (tem_permissao("VENDAS_CADASTRAR")) printf("1 - Nova venda\n");
        if (tem_permissao("VENDAS_VISUALIZAR")) printf("2 - Consultar vendas\n");
        printf("0 - Voltar\n");
        if (!ler_inteiro("Escolha: ",&op)) { op=-1; continue; }
        if (op == 1) {
            int id; char idtxt[24], quantidade[32], valor[32];
            if (!tem_permissao("VENDAS_CADASTRAR")) { puts("Acesso negado."); continue; }
            if (!ler_inteiro("ID do produto: ",&id) || id<=0) { puts("ID invalido."); continue; }
            snprintf(idtxt,sizeof(idtxt),"%d",id);
            ler_string("Quantidade: ",quantidade,sizeof(quantidade));
            ler_string("Valor unitario: ",valor,sizeof(valor));
            char *fim=NULL; double q=strtod(quantidade,&fim);
            if (fim==quantidade || *fim || q<=0) { puts("Quantidade invalida."); continue; }
            double v=strtod(valor,&fim);
            if (fim==valor || *fim || v<0) { puts("Valor invalido."); continue; }
            const char *p[4]={quantidade,idtxt,valor,login_logado};
            PGresult *res=PQexecParams(conn,
                "WITH baixa AS (UPDATE estoque_produto SET quantidade=quantidade-$1::numeric "
                "WHERE id_produto=$2::bigint AND ativo AND quantidade >= $1::numeric RETURNING id_produto,quantidade,estoque_minimo) "
                "INSERT INTO vendas_venda(id_produto,quantidade,valor_unitario,vendedor) "
                "SELECT id_produto,$1::numeric,$3::numeric,$4 FROM baixa RETURNING id_produto",4,NULL,p,NULL,NULL,0);
            if (PQresultStatus(res)==PGRES_TUPLES_OK && PQntuples(res)>0) {
                atualizar_alerta_estoque(idtxt);
                registrar_log("VENDAS","VENDA_CADASTRADA",idtxt); puts("Venda registrada e estoque atualizado.");
            } else if (PQresultStatus(res)==PGRES_TUPLES_OK) puts("Produto inexistente ou saldo insuficiente.");
            else printf("Falha ao registrar venda: %s\n",PQerrorMessage(conn));
            PQclear(res);
        } else if (op == 2) {
            if (!tem_permissao("VENDAS_VISUALIZAR")) { puts("Acesso negado."); continue; }
            PGresult *res=PQexec(conn,"SELECT v.id_venda,v.data_hora,p.nome,v.quantidade,v.valor_unitario,v.vendedor FROM vendas_venda v JOIN estoque_produto p USING(id_produto) ORDER BY v.data_hora DESC LIMIT 100");
            if (PQresultStatus(res)==PGRES_TUPLES_OK) for(int i=0;i<PQntuples(res);i++)
                printf("Venda %s | %s | %s | qtd %s | R$ %s | %s\n",PQgetvalue(res,i,0),PQgetvalue(res,i,1),PQgetvalue(res,i,2),PQgetvalue(res,i,3),PQgetvalue(res,i,4),PQgetvalue(res,i,5));
            else printf("Falha ao consultar vendas: %s\n",PQerrorMessage(conn));
            PQclear(res);
        } else if(op!=0) puts("Opcao invalida.");
    } while(op!=0);
}

static void modulo_compras(void) {
    const char *const permissoes[] = {"COMPRAS_ALERTAS_VISUALIZAR", "COMPRAS_VISUALIZAR", "COMPRAS_EDITAR"};
    if (!tem_alguma_permissao(permissoes, sizeof(permissoes) / sizeof(permissoes[0]))) {
        puts("Acesso negado ao modulo de Compras.");
        return;
    }
    int op=-1;
    do {
        printf("\n========== COMPRAS ==========\n");
        if (tem_permissao("COMPRAS_ALERTAS_VISUALIZAR") || tem_permissao("COMPRAS_VISUALIZAR")) printf("1 - Consultar alertas de reposicao\n");
        if (tem_permissao("COMPRAS_EDITAR")) printf("2 - Registrar recebimento de compra\n");
        printf("0 - Voltar\n");
        if (!ler_inteiro("Escolha: ",&op)) { op=-1; continue; }
        if(op==1) {
            if(!tem_permissao("COMPRAS_ALERTAS_VISUALIZAR") && !tem_permissao("COMPRAS_VISUALIZAR")) { puts("Acesso negado."); continue; }
            PGresult *res=PQexec(conn,"SELECT a.id_alerta,p.id_produto,p.nome,a.quantidade_atual,a.estoque_minimo,a.criado_em FROM compras_alerta_reposicao a JOIN estoque_produto p USING(id_produto) WHERE a.status='ABERTO' ORDER BY a.criado_em");
            if(PQresultStatus(res)==PGRES_TUPLES_OK) for(int i=0;i<PQntuples(res);i++)
                printf("Alerta %s | produto %s (%s) | saldo %s | minimo %s | %s\n",PQgetvalue(res,i,0),PQgetvalue(res,i,2),PQgetvalue(res,i,1),PQgetvalue(res,i,3),PQgetvalue(res,i,4),PQgetvalue(res,i,5));
            else printf("Falha ao consultar alertas: %s\n",PQerrorMessage(conn));
            if(PQntuples(res)==0) puts("Nao ha alertas abertos."); PQclear(res);
        } else if(op==2) {
            int id; char idtxt[24],quantidade[32],custo[32];
            if(!tem_permissao("COMPRAS_EDITAR")) { puts("Acesso negado."); continue; }
            if(!ler_inteiro("ID do produto: ",&id)||id<=0) { puts("ID invalido."); continue; }
            snprintf(idtxt,sizeof(idtxt),"%d",id);
            ler_string("Quantidade recebida: ",quantidade,sizeof(quantidade));
            ler_string("Custo unitario: ",custo,sizeof(custo));
            char *fim=NULL; double q=strtod(quantidade,&fim);
            if(fim==quantidade||*fim||q<=0) { puts("Quantidade invalida."); continue; }
            double c=strtod(custo,&fim); if(fim==custo||*fim||c<0) { puts("Custo invalido."); continue; }
            if(!executar_atualizacao("BEGIN",0,NULL)) continue;
            const char *p[4]={quantidade,idtxt,custo,login_logado};
            int ok=executar_atualizacao("INSERT INTO compras_pedido(id_produto,quantidade,custo_unitario,comprador) VALUES($2::bigint,$1::numeric,$3::numeric,$4)",4,p);
            const char *p2[2]={quantidade,idtxt};
            ok=ok && executar_atualizacao("UPDATE estoque_produto SET quantidade=quantidade+$1::numeric WHERE id_produto=$2::bigint AND ativo",2,p2);
            const char *p3[1]={idtxt};
            ok=ok && executar_atualizacao("UPDATE compras_alerta_reposicao SET status='ATENDIDO',atendido_em=CURRENT_TIMESTAMP WHERE id_produto=$1::bigint AND status='ABERTO'",1,p3);
            if(ok) { executar_atualizacao("COMMIT",0,NULL); registrar_log("COMPRAS","COMPRA_RECEBIDA",idtxt); puts("Compra registrada; saldo atualizado e alerta atendido."); }
            else executar_atualizacao("ROLLBACK",0,NULL);
        } else if(op!=0) puts("Opcao invalida.");
    } while(op!=0);
}

static void relatorio_vendas(void) {
    PGresult *res=PQexec(conn,"SELECT count(*),COALESCE(sum(quantidade*valor_unitario),0) FROM vendas_venda WHERE data_hora >= date_trunc('month',CURRENT_DATE)");
    if(PQresultStatus(res)==PGRES_TUPLES_OK && PQntuples(res)) printf("Vendas no mes: %s | faturamento: R$ %s\n",PQgetvalue(res,0,0),PQgetvalue(res,0,1));
    else printf("Falha no relatorio: %s\n",PQerrorMessage(conn)); PQclear(res);
}

static void relatorio_estoque(void) {
    PGresult *res=PQexec(conn,"SELECT codigo,nome,quantidade,estoque_minimo FROM estoque_produto WHERE ativo ORDER BY (quantidade<=estoque_minimo) DESC,nome");
    if(PQresultStatus(res)==PGRES_TUPLES_OK) for(int i=0;i<PQntuples(res);i++)
        printf("%s | %s | saldo %s | minimo %s%s\n",PQgetvalue(res,i,0),PQgetvalue(res,i,1),PQgetvalue(res,i,2),PQgetvalue(res,i,3),atof(PQgetvalue(res,i,2))<=atof(PQgetvalue(res,i,3))?" | REPOSICAO NECESSARIA":"");
    else printf("Falha no relatorio: %s\n",PQerrorMessage(conn)); PQclear(res);
}

static void relatorio_compras(void) {
    PGresult *res=PQexec(conn,"SELECT count(*),COALESCE(sum(quantidade*custo_unitario),0) FROM compras_pedido WHERE data_hora >= date_trunc('month',CURRENT_DATE)");
    if(PQresultStatus(res)==PGRES_TUPLES_OK && PQntuples(res)) printf("Compras recebidas no mes: %s | custo total: R$ %s\n",PQgetvalue(res,0,0),PQgetvalue(res,0,1));
    else printf("Falha no relatorio: %s\n",PQerrorMessage(conn));
    PQclear(res);
}

static void modulo_relatorios(void) {
    const char *const permissoes[] = {"RELATORIOS_VISUALIZAR", "LOGS_VISUALIZAR"};
    if (!tem_alguma_permissao(permissoes, sizeof(permissoes) / sizeof(permissoes[0]))) {
        puts("Acesso negado aos relatorios e logs.");
        return;
    }
    int op=-1;
    do {
        printf("\n========== GESTAO / RELATORIOS ==========\n");
        if (tem_permissao("RELATORIOS_VISUALIZAR")) printf("1 - Relatorio de vendas e faturamento mensal\n2 - Relatorio de estoque e itens no minimo\n3 - Relatorio de compras\n");
        if (tem_permissao("LOGS_VISUALIZAR")) printf("4 - Consultar logs de auditoria\n");
        printf("0 - Voltar\n");
        if(!ler_inteiro("Escolha: ",&op)) { op=-1; continue; }
        if(op==1) { if(tem_permissao("RELATORIOS_VISUALIZAR")) relatorio_vendas(); else puts("Acesso negado."); }
        else if(op==2) { if(tem_permissao("RELATORIOS_VISUALIZAR")) relatorio_estoque(); else puts("Acesso negado."); }
        else if(op==3) { if(tem_permissao("RELATORIOS_VISUALIZAR")) relatorio_compras(); else puts("Acesso negado."); }
        else if(op==4) { if(tem_permissao("LOGS_VISUALIZAR")) listar_logs(); else puts("Acesso negado."); }
        else if(op!=0) puts("Opcao invalida.");
    } while(op!=0);
}

static void modulo_financeiro(void) {
    if(!tem_permissao("FINANCEIRO_VISUALIZAR")) { puts("Acesso negado ao Financeiro."); return; }
    puts("\n========== FINANCEIRO ==========");
    relatorio_vendas();
    PGresult *res=PQexec(conn,"SELECT count(*),COALESCE(sum(quantidade*custo_unitario),0) FROM compras_pedido WHERE data_hora >= date_trunc('month',CURRENT_DATE)");
    if(PQresultStatus(res)==PGRES_TUPLES_OK && PQntuples(res)) printf("Compras no mes: %s | custos: R$ %s\n",PQgetvalue(res,0,0),PQgetvalue(res,0,1));
    else printf("Falha ao consultar compras: %s\n",PQerrorMessage(conn)); PQclear(res);
}

static void modulo_rh(void) {
    if(!tem_permissao("RH_VISUALIZAR")) { puts("Acesso negado ao RH."); return; }
    PGresult *res=PQexec(conn,"SELECT f.nome,f.setor,u.login,f.status,f.data_admissao FROM funcionario f LEFT JOIN usuario u USING(id_funcionario) ORDER BY f.nome");
    if(PQresultStatus(res)==PGRES_TUPLES_OK) for(int i=0;i<PQntuples(res);i++)
        printf("%s | %s | login: %s | %s | admissao: %s\n",PQgetvalue(res,i,0),PQgetvalue(res,i,1),PQgetvalue(res,i,2),PQgetvalue(res,i,3),PQgetvalue(res,i,4));
    else printf("Falha ao consultar funcionarios: %s\n",PQerrorMessage(conn)); PQclear(res);
}

void menu_usuario(void) {
    int op = -1;
    do {
        limpar_tela();
        printf("========================================\n");
        printf("       ERP TORRA - AREA DO USUARIO\n");
        printf("========================================\n");
        printf("Usuario: %s\n\n", login_logado);
        printf("1 - Consultar minhas permissoes\n");
        if (tem_permissao("VENDAS_VISUALIZAR") || tem_permissao("VENDAS_CADASTRAR")) printf("2 - Vendas\n");
        if (tem_permissao("ESTOQUE_VISUALIZAR") || tem_permissao("ESTOQUE_PRODUTOS_GERENCIAR") || tem_permissao("ESTOQUE_MINIMO_GERENCIAR")) printf("3 - Estoque\n");
        if (tem_permissao("COMPRAS_ALERTAS_VISUALIZAR") || tem_permissao("COMPRAS_VISUALIZAR") || tem_permissao("COMPRAS_EDITAR")) printf("4 - Compras\n");
        if (tem_permissao("FINANCEIRO_VISUALIZAR")) printf("5 - Financeiro\n");
        if (tem_permissao("RELATORIOS_VISUALIZAR") || tem_permissao("LOGS_VISUALIZAR")) printf("6 - Gestao / relatorios / logs\n");
        if (tem_permissao("RH_VISUALIZAR")) printf("7 - Recursos Humanos\n");
        printf("8 - Alterar minha senha\n");
        printf("0 - Sair\n");

        if (!ler_inteiro("Escolha: ", &op)) {
            printf("Entrada inválida. Digite um número.\n");
            pausa();
            op = -1;
            continue;
        }

        switch (op) {
            case 1:
                minhas_permissoes();
                pausa();
                break;
            case 2:
                modulo_vendas();
                pausa();
                break;
            case 3:
                modulo_estoque();
                pausa();
                break;
            case 4:
                modulo_compras();
                pausa();
                break;
            case 5:
                modulo_financeiro();
                pausa();
                break;
            case 6:
                modulo_relatorios();
                pausa();
                break;
            case 7:
                modulo_rh();
                pausa();
                break;
            case 8:
                alterar_senha();
                pausa();
                break;
            case 0:
                break;
            default:
                printf("Opção inválida.\n");
                pausa();
                break;
        }
    } while (op != 0);
}

/*
 * Menu administrativo. Aqui o admin controla usuarios, permissoes, auditoria e acessa os modulos.
 * É o módulo que gerencia a segurança do sistema.
 */
void menu_admin(void) {
    int op = -1;
    do {
        limpar_tela();
        printf("========================================\n");
        printf("       ERP TORRA - SEGURANCA / TI\n");
        printf("========================================\n");
        printf("Administrador: %s\n\n", login_logado);
        printf("1 - Listar usuarios\n");
        printf("2 - Cadastrar usuario\n");
        printf("3 - Bloquear usuario\n");
        printf("4 - Revogar acesso\n");
        printf("5 - Reativar usuario\n");
        printf("6 - Desligar funcionario\n");
        printf("7 - Consultar permissoes de todos os perfis\n");
        printf("8 - Consultar logs\n");
        printf("9 - Alterar minha senha\n");
        printf("10 - Recuperar senha de usuario\n");
        printf("11 - Modulo de Vendas\n12 - Modulo de Estoque\n13 - Modulo de Compras\n14 - Financeiro\n15 - Recursos Humanos\n16 - Relatorios de Gestao\n");
        printf("0 - Sair\n");

        if (!ler_inteiro("Escolha: ", &op)) {
            printf("Entrada inválida. Digite um número.\n");
            pausa();
            op = -1;
            continue;
        }

        switch (op) {
            case 1:
                if (!tem_permissao("USUARIOS_GERENCIAR")) {
                    printf("Acesso negado para listar usuários.\n");
                } else {
                    listar_usuarios();
                }
                pausa();
                break;
            case 2:
                if (!tem_permissao("USUARIOS_GERENCIAR")) {
                    printf("Acesso negado para cadastrar usuário.\n");
                } else {
                    cadastrar_usuario();
                }
                pausa();
                break;
            case 3:
                if (!tem_permissao("USUARIOS_GERENCIAR")) {
                    printf("Acesso negado para bloquear usuário.\n");
                } else {
                    bloquear_usuario();
                }
                pausa();
                break;
            case 4:
                if (!tem_permissao("USUARIOS_GERENCIAR")) {
                    printf("Acesso negado para revogar acesso.\n");
                } else {
                    revogar_usuario();
                }
                pausa();
                break;
            case 5:
                if (!tem_permissao("USUARIOS_GERENCIAR")) {
                    printf("Acesso negado para reativar usuário.\n");
                } else {
                    desbloquear_usuario();
                }
                pausa();
                break;
            case 6:
                if (!tem_permissao("FUNCIONARIOS_DESLIGAR")) {
                    printf("Acesso negado para desligar funcionário.\n");
                } else {
                    desligar_funcionario();
                }
                pausa();
                break;
            case 7:
                if (!tem_permissao("PERMISSOES_GERENCIAR")) {
                    printf("Acesso negado para consultar permissões.\n");
                } else {
                    listar_permissoes();
                }
                pausa();
                break;
            case 8:
                if (!tem_permissao("LOGS_VISUALIZAR")) {
                    printf("Acesso negado aos logs.\n");
                } else {
                    listar_logs();
                }
                pausa();
                break;
            case 9:
                alterar_senha();
                pausa();
                break;
            case 10:
                if (!tem_permissao("USUARIOS_GERENCIAR")) {
                    printf("Acesso negado para recuperar senha.\n");
                } else {
                    redefinir_senha_usuario();
                }
                pausa();
                break;
            case 11: modulo_vendas(); pausa(); break;
            case 12: modulo_estoque(); pausa(); break;
            case 13: modulo_compras(); pausa(); break;
            case 14: modulo_financeiro(); pausa(); break;
            case 15: modulo_rh(); pausa(); break;
            case 16: modulo_relatorios(); pausa(); break;
            case 0:
                break;
            default:
                printf("Opção inválida.\n");
                pausa();
                break;
        }
    } while (op != 0);
}

/*
 * Aqui é o ponto de entrada do programa. Primeiro a gente conecta ao banco,
 * depois o usuário faz login e, por último, decide se entra no menu do admin
 * ou no menu normal.
 */
int main(void) {
#ifdef _WIN32
    /* Deixa o console do Windows em UTF-8 para os acentos aparecerem certo. */
    system("chcp 65001 > nul");
#endif

    if (!conectar())
        return 1;

    if (!conectar_mongodb()) {
        PQfinish(conn);
        desconectar_mongodb();
        return 1;
    }

    for (;;) {
        usuario_logado = 0;
        login_logado[0] = '\0';

        limpar_tela();
        printf("========================================\n");
        printf("              ERP TORRA\n");
        printf("       SEGURANCA / PERMISSOES\n");
        printf("========================================\n\n");

        if (!login())
            continue;

        if (tem_permissao("USUARIOS_GERENCIAR"))
            menu_admin();
        else
            menu_usuario();

        registrar_log("SEGURANCA", "LOGOFF", "Usuario encerrou a sessao");
        usuario_logado = 0;
        login_logado[0] = '\0';
        printf("\nSessao encerrada. Faca login para continuar usando o sistema.\n");
        pausa();
    }
}
