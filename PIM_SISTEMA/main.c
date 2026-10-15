#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libpq-fe.h>

#define DB_HOST "localhost"
#define DB_PORT "5432"
#define DB_NAME "erp_torra"
#define DB_USER "postgres"
#define DB_PASSWORD "vr300games"

PGconn *conn = NULL;
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

/*
 * guarda tudo que acontece no sistema.
 * O tipo é o evento, como LOGIN_SUCESSO ou BLOQUEIO, e a descrição explica o que aconteceu
 */
void registrar_log(const char *tipo, const char *descricao) {
    char idbuf[32];
    snprintf(idbuf, sizeof(idbuf), "%d", usuario_logado);

    const char *params[3] = {idbuf, tipo, descricao};
    const char *sql =
        "INSERT INTO log_acesso(id_usuario, tipo, descricao) "
        "VALUES($1::integer, $2, $3)";

    PGresult *res = PQexecParams(conn, sql, 3, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Aviso: falha ao registrar log: %s\n", PQerrorMessage(conn));
    }
    PQclear(res);
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
        "AND NOT EXISTS ("
        "    SELECT 1 FROM ferias fe "
        "    WHERE fe.id_funcionario = f.id_funcionario "
        "      AND fe.status NOT IN ('CANCELADA','ENCERRADA') "
        "      AND CURRENT_DATE BETWEEN fe.data_inicio AND fe.data_fim"
        ")";

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

        const char *params_log[1] = {descricao};
        const char *logsql =
            "INSERT INTO log_acesso(tipo, descricao) "
            "VALUES('LOGIN_NEGADO', $1)";

        PGresult *logres = PQexecParams(conn, logsql, 1, NULL, params_log, NULL, NULL, 0);
        PQclear(logres);

        printf("\nAcesso negado. Verifique usuário, senha, situação do funcionário ou férias.\n");
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

    registrar_log("LOGIN_SUCESSO", "Login realizado com sucesso");
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
        registrar_log("SENHA_ALTERADA", "Usuário alterou a própria senha");
    } else {
        printf("Não foi possível alterar. Confira a senha atual.\n");
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
        registrar_log("REVOGACAO", descricao);
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
        registrar_log("BLOQUEIO", descricao);
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
        registrar_log("DESBLOQUEIO", descricao);
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
        registrar_log("CADASTRO_USUARIO", descricao);
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
        registrar_log("DESLIGAMENTO", descricao);
    } else if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        printf("Não foi possível desligar (ID não existe, já está desligado ou é o seu próprio).\n");
    } else {
        printf("Erro: %s\n", PQerrorMessage(conn));
    }
    PQclear(res);
}

/*
 * Essa função registra férias para um funcionário. A ideia é salvar a data de início
 * e fim. O status fica em andamento se a data atual estiver dentro do período,
 * senão fica agendada. No login, qualquer férias dentro do período bloqueia
 * o acesso (menos as canceladas).
 */
void cadastrar_ferias(void) {
    int idfunc = -1;
    char idfunc_str[32];
    char inicio[20], fim[20];

    if (!ler_inteiro("ID do funcionário: ", &idfunc)) {
        printf("Informe um número válido para o ID do funcionário.\n");
        return;
    }

    snprintf(idfunc_str, sizeof(idfunc_str), "%d", idfunc);
    ler_string("Data início (AAAA-MM-DD): ", inicio, sizeof(inicio));
    ler_string("Data fim (AAAA-MM-DD): ", fim, sizeof(fim));

    if (inicio[0] == '\0' || fim[0] == '\0') {
        printf("Informe as duas datas.\n");
        return;
    }

    const char *params[3] = {idfunc_str, inicio, fim};
    const char *sql =
        "INSERT INTO ferias(id_funcionario,data_inicio,data_fim,status) "
        "VALUES($1::integer,$2::date,$3::date,"
        "CASE WHEN CURRENT_DATE BETWEEN $2::date AND $3::date "
        "THEN 'EM_ANDAMENTO' ELSE 'AGENDADA' END)";

    PGresult *res = PQexecParams(conn, sql, 3, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        printf("Período de férias cadastrado.\n");
        char descricao[150];
        snprintf(descricao, sizeof(descricao), "Férias cadastradas para o funcionário ID %d (%s a %s)",
                 idfunc, inicio, fim);
        registrar_log("FERIAS", descricao);
    } else {
        printf("Erro (confira o ID do funcionário e as datas): %s\n", PQerrorMessage(conn));
    }
    PQclear(res);
}

/*
 * Aqui eu vejo todas as permissões que existem e a qual perfil cada uma pertence.
 * Isso ajuda a explicar a regra de acesso do sistema.
 */
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
void listar_logs(void) {
    const char *sql =
        "SELECT to_char(l.data_hora, 'YYYY-MM-DD HH24:MI:SS'), COALESCE(u.login,'-'), l.tipo, l.descricao "
        "FROM log_acesso l LEFT JOIN usuario u ON u.id_usuario=l.id_usuario "
        "ORDER BY l.data_hora DESC, l.id_log DESC LIMIT 100";

    PGresult *res = PQexec(conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "%s\n", PQerrorMessage(conn));
        PQclear(res);
        return;
    }

    printf("\nDATA/HORA            LOGIN              TIPO              DESCRICAO\n");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < PQntuples(res); i++) {
        printf("%-20s %-18s %-17s %s\n",
               PQgetvalue(res, i, 0), PQgetvalue(res, i, 1),
               PQgetvalue(res, i, 2), PQgetvalue(res, i, 3));
    }

    PQclear(res);
}

/*
 * Menu do usuário comum. Ele só pode verificar permissões e acessar módulos
 * que o perfil dele permitiu. A regra aqui é simples: se tiver permissão, entra.
 * As opções de férias e logs só funcionam para quem tem a permissão
 * (por exemplo RH e GERENCIA).
 */
void menu_usuario(void) {
    int op = -1;
    do {
        limpar_tela();
        printf("========================================\n");
        printf("       ERP TORRA - AREA DO USUARIO\n");
        printf("========================================\n");
        printf("Usuario: %s\n\n", login_logado);
        printf("1 - Consultar minhas permissoes\n");
        printf("2 - Acessar modulo de vendas\n");
        printf("3 - Acessar modulo de estoque\n");
        printf("4 - Acessar modulo de compras\n");
        printf("5 - Acessar financeiro\n");
        printf("6 - Cadastrar ferias\n");
        printf("7 - Consultar logs\n");
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
                printf(tem_permissao("VENDAS_VISUALIZAR")
                           ? "\nAcesso liberado ao modulo de Vendas.\n"
                           : "\nAcesso negado ao modulo de Vendas.\n");
                pausa();
                break;
            case 3:
                printf(tem_permissao("ESTOQUE_VISUALIZAR")
                           ? "\nAcesso liberado ao modulo de Estoque.\n"
                           : "\nAcesso negado ao modulo de Estoque.\n");
                pausa();
                break;
            case 4:
                printf(tem_permissao("COMPRAS_VISUALIZAR")
                           ? "\nAcesso liberado ao modulo de Compras.\n"
                           : "\nAcesso negado ao modulo de Compras.\n");
                pausa();
                break;
            case 5:
                printf(tem_permissao("FINANCEIRO_VISUALIZAR")
                           ? "\nAcesso liberado ao Financeiro.\n"
                           : "\nAcesso negado ao Financeiro.\n");
                pausa();
                break;
            case 6:
                if (!tem_permissao("FERIAS_GERENCIAR")) {
                    printf("Acesso negado para cadastrar férias.\n");
                } else {
                    cadastrar_ferias();
                }
                pausa();
                break;
            case 7:
                if (!tem_permissao("LOGS_VISUALIZAR")) {
                    printf("Acesso negado aos logs.\n");
                } else {
                    listar_logs();
                }
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
 * Menu administrativo. Aqui o admin controla usuários, perfis, férias e logs.
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
        printf("7 - Cadastrar ferias\n");
        printf("8 - Consultar permissoes de todos os perfis\n");
        printf("9 - Consultar logs\n");
        printf("10 - Alterar minha senha\n");
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
                if (!tem_permissao("USUARIOS_GERENCIAR")) {
                    printf("Acesso negado para desligar funcionário.\n");
                } else {
                    desligar_funcionario();
                }
                pausa();
                break;
            case 7:
                if (!tem_permissao("FERIAS_GERENCIAR")) {
                    printf("Acesso negado para cadastrar férias.\n");
                } else {
                    cadastrar_ferias();
                }
                pausa();
                break;
            case 8:
                if (!tem_permissao("PERMISSOES_GERENCIAR")) {
                    printf("Acesso negado para consultar permissões.\n");
                } else {
                    listar_permissoes();
                }
                pausa();
                break;
            case 9:
                if (!tem_permissao("LOGS_VISUALIZAR")) {
                    printf("Acesso negado aos logs.\n");
                } else {
                    listar_logs();
                }
                pausa();
                break;
            case 10:
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

    limpar_tela();
    printf("========================================\n");
    printf("              ERP TORRA\n");
    printf("       SEGURANCA / PERMISSOES\n");
    printf("========================================\n\n");

    if (!login()) {
        PQfinish(conn);
        return 0;
    }

    if (tem_permissao("USUARIOS_GERENCIAR"))
        menu_admin();
    else
        menu_usuario();

    PQfinish(conn);
    return 0;
}
