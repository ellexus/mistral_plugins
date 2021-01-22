--
-- PostgreSQL database dump
--

-- Dumped from database version 11.5 (Ubuntu 11.5-0ubuntu0.19.04.1)
-- Dumped by pg_dump version 11.4

-- Started on 2019-09-18 17:51:56

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

--
-- TOC entry 207 (class 1259 OID 24762)
-- Name: bandwidth_log_id_seq; Type: SEQUENCE; Schema: public; Owner: mistral
--

CREATE SEQUENCE public.bandwidth_log_id_seq
    START WITH 1
    INCREMENT BY 297642
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.bandwidth_log_id_seq OWNER TO mistral;

SET default_tablespace = '';

SET default_with_oids = false;

--
-- TOC entry 201 (class 1259 OID 24596)
-- Name: bandwidth; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.bandwidth (
    log_id integer DEFAULT nextval('public.bandwidth_log_id_seq'::regclass) NOT NULL,
    plugin_run_id character varying(36) NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) NOT NULL,
    type character varying(8) NOT NULL,
    mistral_record character varying(64) NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10),
    host character varying(256),
    fstype character varying(256),
    fsname character varying(256),
    fshost character varying(256),
    pid integer,
    cpu integer,
    command character varying(1405),
    file_name character varying(1405),
    group_id character varying(256),
    id character varying(256),
    mpi_rank integer
);


ALTER TABLE public.bandwidth OWNER TO mistral;

--
-- TOC entry 206 (class 1259 OID 24759)
-- Name: counts_log_id_seq; Type: SEQUENCE; Schema: public; Owner: mistral
--

CREATE SEQUENCE public.counts_log_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.counts_log_id_seq OWNER TO mistral;

--
-- TOC entry 202 (class 1259 OID 24611)
-- Name: counts; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.counts (
    log_id integer DEFAULT nextval('public.counts_log_id_seq'::regclass) NOT NULL,
    plugin_run_id character varying(36) NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) NOT NULL,
    type character varying(8) NOT NULL,
    mistral_record character varying(64) NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10),
    host character varying(256),
    fstype character varying(256),
    fsname character varying(256),
    fshost character varying(256),
    pid integer,
    cpu integer,
    command character varying(1405),
    file_name character varying(1405),
    group_id character varying(256),
    id character varying(256),
    mpi_rank integer
);


ALTER TABLE public.counts OWNER TO mistral;

--
-- TOC entry 208 (class 1259 OID 24765)
-- Name: cpu_log_id_seq; Type: SEQUENCE; Schema: public; Owner: mistral
--

CREATE SEQUENCE public.cpu_log_id_seq
    START WITH 1
    INCREMENT BY 297642
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.cpu_log_id_seq OWNER TO mistral;

--
-- TOC entry 200 (class 1259 OID 24581)
-- Name: cpu; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.cpu (
    log_id integer DEFAULT nextval('public.cpu_log_id_seq'::regclass) NOT NULL,
    plugin_run_id character varying(36) NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) NOT NULL,
    type character varying(8) NOT NULL,
    mistral_record character varying(64) NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10),
    host character varying(256),
    fstype character varying(256),
    fsname character varying(256),
    fshost character varying(256),
    pid integer,
    cpu integer,
    command character varying(1405),
    file_name character varying(1405),
    group_id character varying(256),
    id character varying(256),
    mpi_rank integer
);


ALTER TABLE public.cpu OWNER TO mistral;

--
-- TOC entry 196 (class 1259 OID 16522)
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
-- TOC entry 197 (class 1259 OID 16528)
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
-- TOC entry 3023 (class 0 OID 0)
-- Dependencies: 197
-- Name: env_env_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: mistral
--

ALTER SEQUENCE public.env_env_id_seq OWNED BY public.env.env_id;


--
-- TOC entry 209 (class 1259 OID 24768)
-- Name: latency_log_id_seq; Type: SEQUENCE; Schema: public; Owner: mistral
--

CREATE SEQUENCE public.latency_log_id_seq
    START WITH 1
    INCREMENT BY 297642
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.latency_log_id_seq OWNER TO mistral;

--
-- TOC entry 203 (class 1259 OID 24626)
-- Name: latency; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.latency (
    log_id integer DEFAULT nextval('public.latency_log_id_seq'::regclass) NOT NULL,
    plugin_run_id character varying(36) NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) NOT NULL,
    type character varying(8) NOT NULL,
    mistral_record character varying(64) NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10),
    host character varying(256),
    fstype character varying(256),
    fsname character varying(256),
    fshost character varying(256),
    pid integer,
    cpu integer,
    command character varying(1405),
    file_name character varying(1405),
    group_id character varying(256),
    id character varying(256),
    mpi_rank integer
);


ALTER TABLE public.latency OWNER TO mistral;

--
-- TOC entry 210 (class 1259 OID 24771)
-- Name: memory_log_id_seq; Type: SEQUENCE; Schema: public; Owner: mistral
--

CREATE SEQUENCE public.memory_log_id_seq
    START WITH 1
    INCREMENT BY 297642
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.memory_log_id_seq OWNER TO mistral;

--
-- TOC entry 204 (class 1259 OID 24641)
-- Name: memory; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.memory (
    log_id integer DEFAULT nextval('public.memory_log_id_seq'::regclass) NOT NULL,
    plugin_run_id character varying(36) NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) NOT NULL,
    type character varying(8) NOT NULL,
    mistral_record character varying(64) NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10),
    host character varying(256),
    fstype character varying(256),
    fsname character varying(256),
    fshost character varying(256),
    pid integer,
    cpu integer,
    command character varying(1405),
    file_name character varying(1405),
    group_id character varying(256),
    id character varying(256),
    mpi_rank integer
);


ALTER TABLE public.memory OWNER TO mistral;

--
-- TOC entry 198 (class 1259 OID 16538)
-- Name: rule_details; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.rule_details (
    rule_id integer NOT NULL,
    rule_label character varying(256) NOT NULL,
    violation_path character varying(256) NOT NULL,
    call_type character varying(55) NOT NULL,
    measurement character varying(20) NOT NULL,
    size_range character varying(64) NOT NULL,
    threshold character varying(64) NOT NULL
);


ALTER TABLE public.rule_details OWNER TO mistral;

--
-- TOC entry 199 (class 1259 OID 16544)
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
-- TOC entry 3024 (class 0 OID 0)
-- Dependencies: 199
-- Name: rule_details_rule_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: mistral
--

ALTER SEQUENCE public.rule_details_rule_id_seq OWNED BY public.rule_details.rule_id;


--
-- TOC entry 211 (class 1259 OID 24774)
-- Name: seek_distance_log_id_seq; Type: SEQUENCE; Schema: public; Owner: mistral
--

CREATE SEQUENCE public.seek_distance_log_id_seq
    START WITH 1
    INCREMENT BY 297642
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.seek_distance_log_id_seq OWNER TO mistral;

--
-- TOC entry 205 (class 1259 OID 24691)
-- Name: seek_distance; Type: TABLE; Schema: public; Owner: mistral
--

CREATE TABLE public.seek_distance (
    log_id integer DEFAULT nextval('public.seek_distance_log_id_seq'::regclass) NOT NULL,
    plugin_run_id character varying(36) NOT NULL,
    rule_id integer NOT NULL,
    time_stamp timestamp with time zone NOT NULL,
    scope character varying(6) NOT NULL,
    type character varying(8) NOT NULL,
    mistral_record character varying(64) NOT NULL,
    measure bigint NOT NULL,
    timeframe character varying(10),
    host character varying(256),
    fstype character varying(256),
    fsname character varying(256),
    fshost character varying(256),
    pid integer,
    cpu integer,
    command character varying(1405),
    file_name character varying(1405),
    group_id character varying(256),
    id character varying(256),
    mpi_rank integer
);


ALTER TABLE public.seek_distance OWNER TO mistral;

--
-- TOC entry 2859 (class 2604 OID 24706)
-- Name: env env_id; Type: DEFAULT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.env ALTER COLUMN env_id SET DEFAULT nextval('public.env_env_id_seq'::regclass);


--
-- TOC entry 2860 (class 2604 OID 24708)
-- Name: rule_details rule_id; Type: DEFAULT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.rule_details ALTER COLUMN rule_id SET DEFAULT nextval('public.rule_details_rule_id_seq'::regclass);


--
-- TOC entry 2877 (class 2606 OID 24604)
-- Name: bandwidth bandwidth_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.bandwidth
    ADD CONSTRAINT bandwidth_pkey PRIMARY KEY (log_id);


--
-- TOC entry 2880 (class 2606 OID 24619)
-- Name: counts count_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.counts
    ADD CONSTRAINT count_pkey PRIMARY KEY (log_id);


--
-- TOC entry 2874 (class 2606 OID 24589)
-- Name: cpu cpu_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.cpu
    ADD CONSTRAINT cpu_pkey PRIMARY KEY (log_id);


--
-- TOC entry 2868 (class 2606 OID 16550)
-- Name: env env_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.env
    ADD CONSTRAINT env_pkey PRIMARY KEY (env_id);


--
-- TOC entry 2884 (class 2606 OID 24634)
-- Name: latency latency_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.latency
    ADD CONSTRAINT latency_pkey PRIMARY KEY (log_id);


--
-- TOC entry 2887 (class 2606 OID 24649)
-- Name: memory memory_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.memory
    ADD CONSTRAINT memory_pkey PRIMARY KEY (log_id);


--
-- TOC entry 2870 (class 2606 OID 16554)
-- Name: rule_details rule_definition; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.rule_details
    ADD CONSTRAINT rule_definition UNIQUE (rule_label, violation_path, call_type, measurement, size_range, threshold);


--
-- TOC entry 2872 (class 2606 OID 16556)
-- Name: rule_details rule_details_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.rule_details
    ADD CONSTRAINT rule_details_pkey PRIMARY KEY (rule_id);


--
-- TOC entry 2890 (class 2606 OID 24699)
-- Name: seek_distance seek_distance_pkey; Type: CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.seek_distance
    ADD CONSTRAINT seek_distance_pkey PRIMARY KEY (log_id);


--
-- TOC entry 2878 (class 1259 OID 24610)
-- Name: fki_bandwidth_rule_id; Type: INDEX; Schema: public; Owner: mistral
--

CREATE INDEX fki_bandwidth_rule_id ON public.bandwidth USING btree (rule_id);


--
-- TOC entry 2881 (class 1259 OID 24625)
-- Name: fki_count_rule_id; Type: INDEX; Schema: public; Owner: mistral
--

CREATE INDEX fki_count_rule_id ON public.counts USING btree (rule_id);


--
-- TOC entry 2875 (class 1259 OID 24595)
-- Name: fki_cpu_rule_id; Type: INDEX; Schema: public; Owner: mistral
--

CREATE INDEX fki_cpu_rule_id ON public.cpu USING btree (rule_id);


--
-- TOC entry 2882 (class 1259 OID 24640)
-- Name: fki_latency_rule_id; Type: INDEX; Schema: public; Owner: mistral
--

CREATE INDEX fki_latency_rule_id ON public.latency USING btree (rule_id);


--
-- TOC entry 2885 (class 1259 OID 24655)
-- Name: fki_memory_rule_id; Type: INDEX; Schema: public; Owner: mistral
--

CREATE INDEX fki_memory_rule_id ON public.memory USING btree (rule_id);


--
-- TOC entry 2888 (class 1259 OID 24705)
-- Name: fki_seek_distance_rule_id; Type: INDEX; Schema: public; Owner: mistral
--

CREATE INDEX fki_seek_distance_rule_id ON public.seek_distance USING btree (rule_id);


--
-- TOC entry 2892 (class 2606 OID 24605)
-- Name: bandwidth bandwidth_rule_id; Type: FK CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.bandwidth
    ADD CONSTRAINT bandwidth_rule_id FOREIGN KEY (rule_id) REFERENCES public.rule_details(rule_id);


--
-- TOC entry 2893 (class 2606 OID 24620)
-- Name: counts count_rule_id; Type: FK CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.counts
    ADD CONSTRAINT count_rule_id FOREIGN KEY (rule_id) REFERENCES public.rule_details(rule_id);


--
-- TOC entry 2891 (class 2606 OID 24590)
-- Name: cpu cpu_rule_id; Type: FK CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.cpu
    ADD CONSTRAINT cpu_rule_id FOREIGN KEY (rule_id) REFERENCES public.rule_details(rule_id);


--
-- TOC entry 2894 (class 2606 OID 24635)
-- Name: latency latency_rule_id; Type: FK CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.latency
    ADD CONSTRAINT latency_rule_id FOREIGN KEY (rule_id) REFERENCES public.rule_details(rule_id);


--
-- TOC entry 2895 (class 2606 OID 24650)
-- Name: memory memory_rule_id; Type: FK CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.memory
    ADD CONSTRAINT memory_rule_id FOREIGN KEY (rule_id) REFERENCES public.rule_details(rule_id);


--
-- TOC entry 2896 (class 2606 OID 24700)
-- Name: seek_distance seek_distance_rule_id; Type: FK CONSTRAINT; Schema: public; Owner: mistral
--

ALTER TABLE ONLY public.seek_distance
    ADD CONSTRAINT seek_distance_rule_id FOREIGN KEY (rule_id) REFERENCES public.rule_details(rule_id);


-- Completed on 2019-09-18 17:51:56

--
-- PostgreSQL database dump complete
--

