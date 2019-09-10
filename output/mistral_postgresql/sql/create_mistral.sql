--
-- PostgreSQL database dump
--

-- Dumped from database version 11.4
-- Dumped by pg_dump version 11.4

-- Started on 2019-08-22 15:03:44

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

DROP DATABASE mistral_log;
--
-- TOC entry 2837 (class 1262 OID 24577)
-- Name: mistral_log; Type: DATABASE; Schema: -; Owner: mistral
--

CREATE DATABASE mistral_log WITH TEMPLATE = template0 ENCODING = 'UTF8';


ALTER DATABASE mistral_log OWNER TO mistral;

\connect mistral_log

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

SET default_with_oids = false;

--
-- TOC entry 201 (class 1259 OID 24609)
-- Name: env; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.env (
    env_id integer NOT NULL,
    plugin_run_id character varying(36) NOT NULL,
    env_name character varying(256) NOT NULL,
    env_value character varying(256)
);


ALTER TABLE public.env OWNER TO mistral;

--
-- TOC entry 200 (class 1259 OID 24607)
-- Name: env_env_id_seq; Type: SEQUENCE; Schema: public; Owner: mistral
--

CREATE SEQUENCE public.env_env_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.env_env_id_seq OWNER TO mistral;

--
-- TOC entry 2838 (class 0 OID 0)
-- Dependencies: 200
-- Name: env_env_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: mistral
--

ALTER SEQUENCE public.env_env_id_seq OWNED BY public.env.env_id;


--
-- TOC entry 199 (class 1259 OID 24598)
-- Name: mistral_log; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.mistral_log (
    log_id integer NOT NULL,
    plugin_run_id character varying(36) NOT NULL,
    rule_id integer NOT NULL,
    time_stamp TIMESTAMPTZ NOT NULL,
    scope character varying(6) NOT NULL,
    type character varying(8) NOT NULL,
    observed character varying(64) NOT NULL,
    host character varying(256),
    pid integer,
    cpu integer,
    command character varying(1405),
    file_name character varying(1405),
    group_id character varying(256),
    id character varying(256),
    mpi_rank integer
);


ALTER TABLE public.mistral_log OWNER TO mistral;

--
-- TOC entry 198 (class 1259 OID 24596)
-- Name: mistral_log_log_id_seq; Type: SEQUENCE; Schema: public; Owner: mistral
--

CREATE SEQUENCE public.mistral_log_log_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.mistral_log_log_id_seq OWNER TO mistral;

--
-- TOC entry 2839 (class 0 OID 0)
-- Dependencies: 198
-- Name: mistral_log_log_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: mistral
--

ALTER SEQUENCE public.mistral_log_log_id_seq OWNED BY public.mistral_log.log_id;


--
-- TOC entry 197 (class 1259 OID 24580)
-- Name: rule_details; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.rule_details (
    rule_id integer NOT NULL,
    rule_label character varying(256) NOT NULL,
    violation_path character varying(256) NOT NULL,
    call_type character varying(45) NOT NULL,
    measurement character varying(13) NOT NULL,
    size_range character varying(64) NOT NULL,
    threshold character varying(64) NOT NULL
);


ALTER TABLE public.rule_details OWNER TO mistral;

--
-- TOC entry 196 (class 1259 OID 24578)
-- Name: rule_details_rule_id_seq; Type: SEQUENCE; Schema: public; Owner: mistral
--

CREATE SEQUENCE public.rule_details_rule_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.rule_details_rule_id_seq OWNER TO mistral;

--
-- TOC entry 2840 (class 0 OID 0)
-- Dependencies: 196
-- Name: rule_details_rule_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: mistral
--

ALTER SEQUENCE public.rule_details_rule_id_seq OWNED BY public.rule_details.rule_id;


--
-- TOC entry 2702 (class 2604 OID 24612)
-- Name: env env_id; Type: DEFAULT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.env ALTER COLUMN env_id SET DEFAULT nextval('public.env_env_id_seq'::regclass);


--
-- TOC entry 2701 (class 2604 OID 24601)
-- Name: mistral_log log_id; Type: DEFAULT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.mistral_log ALTER COLUMN log_id SET DEFAULT nextval('public.mistral_log_log_id_seq'::regclass);


--
-- TOC entry 2700 (class 2604 OID 24583)
-- Name: rule_details rule_id; Type: DEFAULT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.rule_details ALTER COLUMN rule_id SET DEFAULT nextval('public.rule_details_rule_id_seq'::regclass);


--
-- TOC entry 2710 (class 2606 OID 24617)
-- Name: env env_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.env
    ADD CONSTRAINT env_pkey PRIMARY KEY (env_id);


--
-- TOC entry 2708 (class 2606 OID 24606)
-- Name: mistral_log mistral_log_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.mistral_log
    ADD CONSTRAINT mistral_log_pkey PRIMARY KEY (log_id);


--
-- TOC entry 2704 (class 2606 OID 24590)
-- Name: rule_details rule_definition; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.rule_details
    ADD CONSTRAINT rule_definition UNIQUE (rule_label, violation_path, call_type, measurement, size_range, threshold);


--
-- TOC entry 2706 (class 2606 OID 24588)
-- Name: rule_details rule_details_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.rule_details
    ADD CONSTRAINT rule_details_pkey PRIMARY KEY (rule_id);


-- Completed on 2019-08-22 15:03:44

-- Table: public.cpu

-- DROP TABLE public.cpu;

CREATE TABLE public.cpu
(
    log_id integer NOT NULL DEFAULT nextval('mistral_log_log_id_seq'::regclass),
    plugin_run_id character varying(36) COLLATE pg_catalog."default" NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) COLLATE pg_catalog."default" NOT NULL,
    type character varying(8) COLLATE pg_catalog."default" NOT NULL,
    mistral_record character varying(64) COLLATE pg_catalog."default" NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10) COLLATE pg_catalog."default",
    host character varying(256) COLLATE pg_catalog."default",
    pid integer,
    cpu integer,
    command character varying(1405) COLLATE pg_catalog."default",
    file_name character varying(1405) COLLATE pg_catalog."default",
    group_id character varying(256) COLLATE pg_catalog."default",
    id character varying(256) COLLATE pg_catalog."default",
    mpi_rank integer,
    CONSTRAINT cpu_pkey PRIMARY KEY (log_id),
    CONSTRAINT cpu_rule_id FOREIGN KEY (rule_id)
        REFERENCES public.rule_details (rule_id) MATCH SIMPLE
        ON UPDATE NO ACTION
        ON DELETE NO ACTION
)
WITH (
    OIDS = FALSE
)
TABLESPACE pg_default;

ALTER TABLE public.cpu
    OWNER to mistral;

-- Index: fki_cpu_rule_id

-- DROP INDEX public.fki_cpu_rule_id;

CREATE INDEX fki_cpu_rule_id
    ON public.cpu USING btree
    (rule_id)
    TABLESPACE pg_default;

-- Table: public.bandwidth

-- DROP TABLE public.bandwidth;

CREATE TABLE public.bandwidth
(
    log_id integer NOT NULL DEFAULT nextval('mistral_log_log_id_seq'::regclass),
    plugin_run_id character varying(36) COLLATE pg_catalog."default" NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) COLLATE pg_catalog."default" NOT NULL,
    type character varying(8) COLLATE pg_catalog."default" NOT NULL,
    mistral_record character varying(64) COLLATE pg_catalog."default" NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10) COLLATE pg_catalog."default",
    host character varying(256) COLLATE pg_catalog."default",
    pid integer,
    cpu integer,
    command character varying(1405) COLLATE pg_catalog."default",
    file_name character varying(1405) COLLATE pg_catalog."default",
    group_id character varying(256) COLLATE pg_catalog."default",
    id character varying(256) COLLATE pg_catalog."default",
    mpi_rank integer,
    CONSTRAINT bandwidth_pkey PRIMARY KEY (log_id),
    CONSTRAINT bandwidth_rule_id FOREIGN KEY (rule_id)
        REFERENCES public.rule_details (rule_id) MATCH SIMPLE
        ON UPDATE NO ACTION
        ON DELETE NO ACTION
)
WITH (
    OIDS = FALSE
)
TABLESPACE pg_default;

ALTER TABLE public.bandwidth
    OWNER to mistral;

-- Index: fki_bandwidth_rule_id

-- DROP INDEX public.fki_bandwidth_rule_id;

CREATE INDEX fki_bandwidth_rule_id
    ON public.bandwidth USING btree
    (rule_id)
    TABLESPACE pg_default;

-- Table: public.count

-- DROP TABLE public.count;

CREATE TABLE public.count
(
    log_id integer NOT NULL DEFAULT nextval('mistral_log_log_id_seq'::regclass),
    plugin_run_id character varying(36) COLLATE pg_catalog."default" NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) COLLATE pg_catalog."default" NOT NULL,
    type character varying(8) COLLATE pg_catalog."default" NOT NULL,
    mistral_record character varying(64) COLLATE pg_catalog."default" NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10) COLLATE pg_catalog."default",
    host character varying(256) COLLATE pg_catalog."default",
    pid integer,
    cpu integer,
    command character varying(1405) COLLATE pg_catalog."default",
    file_name character varying(1405) COLLATE pg_catalog."default",
    group_id character varying(256) COLLATE pg_catalog."default",
    id character varying(256) COLLATE pg_catalog."default",
    mpi_rank integer,
    CONSTRAINT count_pkey PRIMARY KEY (log_id),
    CONSTRAINT count_rule_id FOREIGN KEY (rule_id)
        REFERENCES public.rule_details (rule_id) MATCH SIMPLE
        ON UPDATE NO ACTION
        ON DELETE NO ACTION
)
WITH (
    OIDS = FALSE
)
TABLESPACE pg_default;

ALTER TABLE public.count
    OWNER to mistral;

-- Index: fki_count_rule_id

-- DROP INDEX public.fki_count_rule_id;

CREATE INDEX fki_count_rule_id
    ON public.count USING btree
    (rule_id)
    TABLESPACE pg_default;

-- Table: public.latency

-- DROP TABLE public.latency;

CREATE TABLE public.latency
(
    log_id integer NOT NULL DEFAULT nextval('mistral_log_log_id_seq'::regclass),
    plugin_run_id character varying(36) COLLATE pg_catalog."default" NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) COLLATE pg_catalog."default" NOT NULL,
    type character varying(8) COLLATE pg_catalog."default" NOT NULL,
    mistral_record character varying(64) COLLATE pg_catalog."default" NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10) COLLATE pg_catalog."default",
    host character varying(256) COLLATE pg_catalog."default",
    pid integer,
    cpu integer,
    command character varying(1405) COLLATE pg_catalog."default",
    file_name character varying(1405) COLLATE pg_catalog."default",
    group_id character varying(256) COLLATE pg_catalog."default",
    id character varying(256) COLLATE pg_catalog."default",
    mpi_rank integer,
    CONSTRAINT latency_pkey PRIMARY KEY (log_id),
    CONSTRAINT latency_rule_id FOREIGN KEY (rule_id)
        REFERENCES public.rule_details (rule_id) MATCH SIMPLE
        ON UPDATE NO ACTION
        ON DELETE NO ACTION
)
WITH (
    OIDS = FALSE
)
TABLESPACE pg_default;

ALTER TABLE public.latency
    OWNER to mistral;

-- Index: fki_latency_rule_id

-- DROP INDEX public.fki_latency_rule_id;

CREATE INDEX fki_latency_rule_id
    ON public.latency USING btree
    (rule_id)
    TABLESPACE pg_default;

-- Table: public.memory

-- DROP TABLE public.memory;

CREATE TABLE public.memory
(
    log_id integer NOT NULL DEFAULT nextval('mistral_log_log_id_seq'::regclass),
    plugin_run_id character varying(36) COLLATE pg_catalog."default" NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) COLLATE pg_catalog."default" NOT NULL,
    type character varying(8) COLLATE pg_catalog."default" NOT NULL,
    mistral_record character varying(64) COLLATE pg_catalog."default" NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10) COLLATE pg_catalog."default",
    host character varying(256) COLLATE pg_catalog."default",
    pid integer,
    cpu integer,
    command character varying(1405) COLLATE pg_catalog."default",
    file_name character varying(1405) COLLATE pg_catalog."default",
    group_id character varying(256) COLLATE pg_catalog."default",
    id character varying(256) COLLATE pg_catalog."default",
    mpi_rank integer,
    CONSTRAINT memory_pkey PRIMARY KEY (log_id),
    CONSTRAINT memory_rule_id FOREIGN KEY (rule_id)
        REFERENCES public.rule_details (rule_id) MATCH SIMPLE
        ON UPDATE NO ACTION
        ON DELETE NO ACTION
)
WITH (
    OIDS = FALSE
)
TABLESPACE pg_default;

ALTER TABLE public.memory
    OWNER to mistral;

-- Index: fki_memory_rule_id

-- DROP INDEX public.fki_memory_rule_id;

CREATE INDEX fki_memory_rule_id
    ON public.memory USING btree
    (rule_id)
    TABLESPACE pg_default;
--
-- PostgreSQL database dump complete
--

