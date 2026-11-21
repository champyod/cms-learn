SELECT format('CREATE ROLE %I NOLOGIN', name)
FROM (VALUES ('cmslearn_app'), ('cmslearn_judge'), ('cmslearn_migrate'), ('cmslearn_backup'),
         ('cmslearn_generator'), ('cmslearn_certifier')) AS roles(name)
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = roles.name)
\gexec

-- One LOGIN role per service (ADR 0058). A service role holds no grant of its own: it is a member of exactly one
-- capability group above, so what a compromised service can reach is what that group was granted and nothing else.
--
-- The password never appears in this file, in the compose file or in the environment: it is set from a variable the
-- deploy step passes after reading the service's own secret file, and a service that has no password cannot log in.
\if :{?api_password}
SELECT format('CREATE ROLE %I LOGIN PASSWORD %L', role_name, role_password)
FROM (VALUES
        ('cmslearn_api_user',       :'api_password'),
        ('cmslearn_generator_user', :'generator_password'),
        ('cmslearn_certifier_user', :'certifier_password'),
        ('cmslearn_judge_user',     :'judge_password'),
        ('cmslearn_bank_user',      :'bank_password'),
        ('cmslearn_backup_user',    :'backup_password')
     ) AS services(role_name, role_password)
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = services.role_name)
\gexec
\else
-- No password supplied (a schema render, a test cluster): the roles exist but cannot log in until the deploy step sets one.
SELECT format('CREATE ROLE %I NOLOGIN', name)
FROM (VALUES ('cmslearn_api_user'), ('cmslearn_generator_user'), ('cmslearn_certifier_user'),
             ('cmslearn_judge_user'), ('cmslearn_bank_user'), ('cmslearn_backup_user')) AS services(name)
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = services.name)
\gexec
\endif

-- The login roles carry no grant of their own: each is a member of the one capability group its service works through,
-- which is why a grant added to a group reaches every service that group serves and nothing else.
GRANT cmslearn_app TO cmslearn_api_user;
GRANT cmslearn_generator TO cmslearn_generator_user;
GRANT cmslearn_certifier TO cmslearn_certifier_user;
GRANT cmslearn_judge TO cmslearn_judge_user;
-- The bank seeds content, so it reads and writes the content tables through the app group and nothing else.
GRANT cmslearn_app TO cmslearn_bank_user;
GRANT cmslearn_backup TO cmslearn_backup_user;
