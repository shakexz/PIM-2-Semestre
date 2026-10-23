-- Aqui a gente cria as tabelas, define as regras e já grava os perfis iniciais.

CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- Tabela de funcionário.
-- Aqui guardamos dados básicos da pessoa que trabalha na empresa.
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

-- Tabela de logs.
-- Contrato compartilhável: actor_login e sistema_origem preservam a identidade
-- mesmo quando os grupos usam bancos e IDs de usuário diferentes.
CREATE TABLE IF NOT EXISTS log_acesso (
    id_log SERIAL PRIMARY KEY,
    id_usuario INTEGER REFERENCES usuario(id_usuario) ON DELETE SET NULL,
    actor_login VARCHAR(100),
    sistema_origem VARCHAR(50) NOT NULL DEFAULT 'ERP_TORRA',
    id_evento UUID NOT NULL DEFAULT gen_random_uuid(),
    data_hora TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    setor VARCHAR(50) NOT NULL DEFAULT 'SISTEMA',
    tipo VARCHAR(30) NOT NULL,
    descricao TEXT
);

-- Mantém o script compatível com bancos criados antes da classificação por setor.
ALTER TABLE log_acesso ADD COLUMN IF NOT EXISTS setor VARCHAR(50) NOT NULL DEFAULT 'SISTEMA';
ALTER TABLE log_acesso ADD COLUMN IF NOT EXISTS actor_login VARCHAR(100);
ALTER TABLE log_acesso ADD COLUMN IF NOT EXISTS sistema_origem VARCHAR(50) NOT NULL DEFAULT 'ERP_TORRA';
ALTER TABLE log_acesso ADD COLUMN IF NOT EXISTS id_evento UUID NOT NULL DEFAULT gen_random_uuid();
UPDATE log_acesso l SET actor_login = u.login FROM usuario u
WHERE l.id_usuario = u.id_usuario AND l.actor_login IS NULL;

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
CREATE UNIQUE INDEX IF NOT EXISTS idx_log_evento ON log_acesso(id_evento);

-- Modulos operacionais. As tabelas usam prefixos para facilitar a integracao
-- posterior com os bancos dos grupos de Vendas, Estoque e Compras.
CREATE TABLE IF NOT EXISTS estoque_produto (
    id_produto BIGSERIAL PRIMARY KEY,
    codigo VARCHAR(40) UNIQUE NOT NULL,
    nome VARCHAR(120) NOT NULL,
    quantidade NUMERIC(12,2) NOT NULL DEFAULT 0 CHECK (quantidade >= 0),
    estoque_minimo NUMERIC(12,2) NOT NULL DEFAULT 0 CHECK (estoque_minimo >= 0),
    ativo BOOLEAN NOT NULL DEFAULT TRUE,
    criado_em TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS vendas_venda (
    id_venda BIGSERIAL PRIMARY KEY,
    id_produto BIGINT NOT NULL REFERENCES estoque_produto(id_produto),
    quantidade NUMERIC(12,2) NOT NULL CHECK (quantidade > 0),
    valor_unitario NUMERIC(12,2) NOT NULL CHECK (valor_unitario >= 0),
    vendedor VARCHAR(100) NOT NULL,
    data_hora TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS compras_alerta_reposicao (
    id_alerta BIGSERIAL PRIMARY KEY,
    id_produto BIGINT NOT NULL REFERENCES estoque_produto(id_produto),
    quantidade_atual NUMERIC(12,2) NOT NULL,
    estoque_minimo NUMERIC(12,2) NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'ABERTO' CHECK (status IN ('ABERTO','ATENDIDO')),
    criado_em TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    atendido_em TIMESTAMP
);
CREATE UNIQUE INDEX IF NOT EXISTS uq_alerta_aberto_produto
    ON compras_alerta_reposicao(id_produto) WHERE status = 'ABERTO';

CREATE TABLE IF NOT EXISTS compras_pedido (
    id_pedido BIGSERIAL PRIMARY KEY,
    id_produto BIGINT NOT NULL REFERENCES estoque_produto(id_produto),
    quantidade NUMERIC(12,2) NOT NULL CHECK (quantidade > 0),
    custo_unitario NUMERIC(12,2) NOT NULL DEFAULT 0 CHECK (custo_unitario >= 0),
    comprador VARCHAR(100) NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'RECEBIDO' CHECK (status IN ('RECEBIDO')),
    data_hora TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_vendas_data ON vendas_venda(data_hora);
CREATE INDEX IF NOT EXISTS idx_compras_pedidos_data ON compras_pedido(data_hora);

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
('VENDAS_CADASTRAR', 'Cadastrar vendas'),
('ESTOQUE_VISUALIZAR', 'Visualizar estoque'),
('ESTOQUE_PRODUTOS_GERENCIAR', 'Cadastrar e editar produtos'),
('ESTOQUE_MINIMO_GERENCIAR', 'Definir estoque mínimo'),
('COMPRAS_ALERTAS_VISUALIZAR', 'Visualizar alertas de reposição enviados pelo estoque'),
('COMPRAS_VISUALIZAR', 'Visualizar compras'),
('COMPRAS_EDITAR', 'Cadastrar ou editar compras'),
('FINANCEIRO_VISUALIZAR', 'Visualizar financeiro'),
('RH_VISUALIZAR', 'Visualizar RH'),
('USUARIOS_GERENCIAR', 'Gerenciar usuários'),
('PERMISSOES_GERENCIAR', 'Gerenciar perfis e permissões'),
('LOGS_VISUALIZAR', 'Visualizar logs'),
('RELATORIOS_VISUALIZAR', 'Visualizar relatórios gerenciais'),
('FUNCIONARIOS_DESLIGAR', 'Desativar funcionários; exclusivo do administrador')
ON CONFLICT (nome) DO NOTHING;

-- O perfil ADMIN vai ter todas as permissões.
-- Reaplica a matriz abaixo para remover concessoes antigas nos perfis padrao.
-- Perfis personalizados, fora desta lista, nao sao modificados.
DELETE FROM perfil_permissao pp
USING perfil p
WHERE pp.id_perfil = p.id_perfil
  AND p.nome IN ('ADMIN','VENDAS','ESTOQUE','COMPRAS','FINANCEIRO','RH','GERENCIA');

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p CROSS JOIN permissao pe
WHERE p.nome = 'ADMIN'
ON CONFLICT DO NOTHING;

-- Cada perfil específico recebe só as permissões dele.
INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('VENDAS_VISUALIZAR','VENDAS_CADASTRAR')
WHERE p.nome = 'VENDAS'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('ESTOQUE_VISUALIZAR','ESTOQUE_PRODUTOS_GERENCIAR','ESTOQUE_MINIMO_GERENCIAR')
WHERE p.nome = 'ESTOQUE'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('COMPRAS_VISUALIZAR','COMPRAS_EDITAR','COMPRAS_ALERTAS_VISUALIZAR')
WHERE p.nome = 'COMPRAS'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('FINANCEIRO_VISUALIZAR')
WHERE p.nome = 'FINANCEIRO'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('RH_VISUALIZAR')
WHERE p.nome = 'RH'
ON CONFLICT DO NOTHING;

INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome IN ('LOGS_VISUALIZAR','RELATORIOS_VISUALIZAR')
WHERE p.nome = 'GERENCIA'
ON CONFLICT DO NOTHING;

-- A ação de desligar funcionário permanece exclusiva do perfil ADMIN.
INSERT INTO perfil_permissao (id_perfil, id_permissao)
SELECT p.id_perfil, pe.id_permissao
FROM perfil p JOIN permissao pe ON pe.nome = 'FUNCIONARIOS_DESLIGAR'
WHERE p.nome = 'ADMIN'
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
