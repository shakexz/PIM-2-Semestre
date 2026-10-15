-- Aqui a gente cria as tabelas, define as regras e já grava os perfis iniciais.

CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- Tabela de funcionário.
-- Aqui guardamos dados básicos da pessoa que trabalha na empresa.
-- O status do funcionário é ATIVO ou DESLIGADO. As férias ficam na tabela ferias.
CREATE TABLE IF NOT EXISTS funcionario (
    id_funcionario SERIAL PRIMARY KEY,
    nome VARCHAR(100) NOT NULL,
    cpf VARCHAR(11) UNIQUE NOT NULL,
    setor VARCHAR(50) NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'ATIVO'
        CHECK (status IN ('ATIVO','DESLIGADO')),
    data_admissao DATE NOT NULL,
    data_desligamento DATE
);

-- Tabela de usuário do sistema.
-- Cada usuário está ligado a um funcionário e tem login e senha.
CREATE TABLE IF NOT EXISTS usuario (
    id_usuario SERIAL PRIMARY KEY,
    id_funcionario INTEGER NOT NULL UNIQUE
        REFERENCES funcionario(id_funcionario),
    login VARCHAR(100) UNIQUE NOT NULL,
    senha_hash VARCHAR(255) NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'ATIVO'
        CHECK (status IN ('ATIVO','BLOQUEADO','REVOGADO')),
    ultimo_acesso TIMESTAMP
);

-- Tabela de perfis.
-- Exemplo: ADMIN, VENDAS, ESTOQUE, RH etc.
CREATE TABLE IF NOT EXISTS perfil (
    id_perfil SERIAL PRIMARY KEY,
    nome VARCHAR(50) UNIQUE NOT NULL,
    descricao VARCHAR(255)
);

-- Tabela de permissões.
-- Cada permissão representa uma ação específica do sistema.
CREATE TABLE IF NOT EXISTS permissao (
    id_permissao SERIAL PRIMARY KEY,
    nome VARCHAR(100) UNIQUE NOT NULL,
    descricao VARCHAR(255)
);

-- Relação entre usuário e perfil.
-- Um usuário pode ter vários perfis.
CREATE TABLE IF NOT EXISTS usuario_perfil (
    id_usuario INTEGER NOT NULL REFERENCES usuario(id_usuario) ON DELETE CASCADE,
    id_perfil INTEGER NOT NULL REFERENCES perfil(id_perfil) ON DELETE CASCADE,
    PRIMARY KEY (id_usuario, id_perfil)
);

-- Relação entre perfil e permissão.
-- Essa parte decide o que cada perfil pode fazer.
CREATE TABLE IF NOT EXISTS perfil_permissao (
    id_perfil INTEGER NOT NULL REFERENCES perfil(id_perfil) ON DELETE CASCADE,
    id_permissao INTEGER NOT NULL REFERENCES permissao(id_permissao) ON DELETE CASCADE,
    PRIMARY KEY (id_perfil, id_permissao)
);

-- Tabela de férias.
-- Guardamos as datas do período e um status (AGENDADA, EM_ANDAMENTO, ENCERRADA ou CANCELADA).
-- No login, qualquer férias dentro do período bloqueia o acesso, menos as CANCELADA/ENCERRADA.
CREATE TABLE IF NOT EXISTS ferias (
    id_ferias SERIAL PRIMARY KEY,
    id_funcionario INTEGER NOT NULL REFERENCES funcionario(id_funcionario),
    data_inicio DATE NOT NULL,
    data_fim DATE NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'AGENDADA'
        CHECK (status IN ('AGENDADA','EM_ANDAMENTO','ENCERRADA','CANCELADA')),
    CHECK (data_fim >= data_inicio)
);

-- Tabela de logs.
-- Toda ação importante do sistema fica registrada aqui (quem fez, quando e o quê).
CREATE TABLE IF NOT EXISTS log_acesso (
    id_log SERIAL PRIMARY KEY,
    id_usuario INTEGER REFERENCES usuario(id_usuario) ON DELETE SET NULL,
    data_hora TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    setor VARCHAR(50) NOT NULL DEFAULT 'SISTEMA',
    tipo VARCHAR(30) NOT NULL,
    descricao TEXT
);

-- Mantém o script compatível com bancos criados antes da classificação por setor.
ALTER TABLE log_acesso ADD COLUMN IF NOT EXISTS setor VARCHAR(50) NOT NULL DEFAULT 'SISTEMA';

-- Migra os valores se o banco já usava a coluna modulo.
DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_name = 'log_acesso' AND column_name = 'modulo'
    ) THEN
        EXECUTE 'UPDATE log_acesso SET setor = modulo WHERE setor = ''SISTEMA'' AND modulo IS NOT NULL';
    END IF;
END $$;

-- Índices.
-- (O login não precisa de índice extra porque o UNIQUE já cria um.)
CREATE INDEX IF NOT EXISTS idx_log_usuario ON log_acesso(id_usuario);
CREATE INDEX IF NOT EXISTS idx_log_data_setor ON log_acesso(data_hora, setor);
CREATE INDEX IF NOT EXISTS idx_ferias_funcionario ON ferias(id_funcionario);

-- --- PERFIS INICIAIS ---
-- Cada perfil representa um tipo de acesso no sistema.
INSERT INTO perfil (nome, descricao) VALUES
('ADMIN', 'Administrador do sistema'),
('VENDAS', 'Acesso ao módulo de vendas'),
('ESTOQUE', 'Acesso ao módulo de estoque'),
('COMPRAS', 'Acesso ao módulo de compras'),
('FINANCEIRO', 'Acesso aos módulos financeiros'),
('RH', 'Acesso ao módulo de recursos humanos'),
('GERENCIA', 'Acesso a relatórios gerenciais')
ON CONFLICT (nome) DO NOTHING;

-- --- PERMISSÕES ---
-- As permissões são as ações que cada perfil pode fazer.
INSERT INTO permissao (nome, descricao) VALUES
('VENDAS_VISUALIZAR', 'Visualizar vendas'),
('VENDAS_EDITAR', 'Cadastrar ou editar vendas'),
('ESTOQUE_VISUALIZAR', 'Visualizar estoque'),
('ESTOQUE_EDITAR', 'Cadastrar ou editar estoque'),
('COMPRAS_VISUALIZAR', 'Visualizar compras'),
('COMPRAS_EDITAR', 'Cadastrar ou editar compras'),
('FINANCEIRO_VISUALIZAR', 'Visualizar financeiro'),
('RH_VISUALIZAR', 'Visualizar RH'),
('USUARIOS_GERENCIAR', 'Gerenciar usuários'),
('PERMISSOES_GERENCIAR', 'Gerenciar perfis e permissões'),
('LOGS_VISUALIZAR', 'Visualizar logs'),
('FERIAS_GERENCIAR', 'Gerenciar férias')
ON CONFLICT (nome) DO NOTHING;

-- O perfil ADMIN vai ter todas as permissões.
INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p CROSS JOIN permissao pe
WHERE p.nome = 'ADMIN'
ON CONFLICT DO NOTHING;

-- Cada perfil específico recebe só as permissões dele.
INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('VENDAS_VISUALIZAR','VENDAS_EDITAR')
WHERE p.nome = 'VENDAS'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('ESTOQUE_VISUALIZAR','ESTOQUE_EDITAR')
WHERE p.nome = 'ESTOQUE'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('COMPRAS_VISUALIZAR','COMPRAS_EDITAR')
WHERE p.nome = 'COMPRAS'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('FINANCEIRO_VISUALIZAR')
WHERE p.nome = 'FINANCEIRO'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('RH_VISUALIZAR','FERIAS_GERENCIAR')
WHERE p.nome = 'RH'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('LOGS_VISUALIZAR')
WHERE p.nome = 'GERENCIA'
ON CONFLICT DO NOTHING;

-- --- USUÁRIO ADMIN INICIAL ---
-- Cadastro do admin (login: admin / senha: admin123, depois troque pela opção "Alterar minha senha").
INSERT INTO funcionario (nome, cpf, setor, data_admissao)
VALUES ('Administrador do Sistema', '00000000000', 'TI', CURRENT_DATE)
ON CONFLICT (cpf) DO NOTHING;

INSERT INTO usuario (id_funcionario, login, senha_hash)
SELECT f.id_funcionario, 'admin', crypt('admin123', gen_salt('bf'))
FROM funcionario f
WHERE f.cpf = '00000000000'
  AND NOT EXISTS (SELECT 1 FROM usuario u WHERE u.login = 'admin');

INSERT INTO usuario_perfil (id_usuario, id_perfil)
SELECT u.id_usuario, p.id_perfil
FROM usuario u CROSS JOIN perfil p
WHERE u.login = 'admin' AND p.nome = 'ADMIN'
ON CONFLICT DO NOTHING;
