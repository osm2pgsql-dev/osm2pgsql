--
-- PostgreSQL database dump
--

SET client_encoding = 'UTF8';
SET standard_conforming_strings = off;
SET check_function_bodies = false;
SET client_min_messages = warning;
SET escape_string_warning = off;

SET search_path = public, pg_catalog;

SET default_tablespace = '';

SET default_with_oids = false;

--
-- Name: gb_postcodearea; Type: TABLE; Schema: public; Owner: twain; Tablespace: 
--

CREATE TABLE gb_postcodearea (
    postcodeareaid character varying(2) NOT NULL,
    postcodeareaname text,
    postcodearearegion text
);

--
-- Data for Name: gb_postcodearea; Type: TABLE DATA; Schema: public; Owner: twain
--

COPY gb_postcodearea (postcodeareaid, postcodeareaname, postcodearearegion) FROM stdin;
BB	Blackburn	England
HS	Outer Hebrides	Scotland
KY	Kirkcaldy	Scotland
CB	Cambridge	England
HU	Hull	England
ST	Stoke-on-Trent	England
EN	Enfield	England
ME	Medway	England
KT	Kingston upon Thames	England
LU	Luton	England
TN	Tonbridge	England
DT	Dorchester	England
NE	Newcastle upon Tyne	England
TW	Twickenham	England
KA	Kilmarnock	Scotland
DD	Dundee	Scotland
EX	Exeter	England
TF	Telford	England
AB	Aberdeen	Scotland
CV	Coventry	England
IM	Isle of Man	Isle of Man
WV	Wolverhampton	England
G	Glasgow	Scotland
NW	London NW	England
PR	Preston	England
S	Sheffield	England
HA	Harrow	England
LS	Leeds	England
LA	Lancaster	England
W	London W	England
RH	Redhill	England
WS	Walsall	England
BN	Brighton	England
TD	Tweeddale	Scotland
WC	London WC	England
HD	Huddersfield	England
DN	Doncaster	England
FK	Falkirk	Scotland
DE	Derby	England
SM	Sutton	England
BA	Bath	England
WA	Warrington	England
PH	Perth	Scotland
TQ	Torquay	England
GU	Guildford	England
SW	London SW	England
GL	Gloucester	England
NP	Newport	Wales
B	Birmingham	England
EC	London EC	England
DH	Durham	England
BS	Bristol	England
BL	Bolton	England
CR	Croydon	England
PO	Portsmouth	England
SE	London SE	England
PE	Peterborough	England
NR	Norwich	England
WR	Worcester	England
BR	Bromley	England
HP	Hemel Hempstead	England
LE	Leicester	England
NN	Northampton	England
OL	Oldham	England
CO	Colchester	England
RM	Romford	England
SK	Stockport	England
DG	Dumfries and Galloway	Scotland
IP	Ipswich	England
SP	Salisbury	England
JE	Jersey	Channel Islands
CH	Chester	England, Wales
LL	Llandudno	Wales
GY	Guernsey	Channel Islands
HG	Harrogate	England
SO	Southampton	England
DY	Dudley	England
SA	Swansea	Wales
CA	Carlisle	England
OX	Oxford	England
HR	Hereford	England
HX	Halifax	England
IV	Inverness	Scotland
BH	Bournemouth	England
SR	Sunderland	England
SN	Swindon	England
N	London N	England
WD	Watford	England
LN	Lincoln	England
PA	Paisley	Scotland
PL	Plymouth	England
L	Liverpool	England
CM	Chelmsford	England
KW	Kirkwall	Scotland
SG	Stevenage	England
WF	Wakefield	England
WN	Wigan	England
CF	Cardiff	Wales
LD	Llandrindod Wells	Wales
MK	Milton Keynes	England
E	London E	England
YO	York	England
SS	Southend on Sea	England
NG	Nottingham	England
TR	Truro	England
AL	St Albans	England
IG	Ilford	England
BT	Belfast	Northern Ireland
EH	Edinburgh	Scotland
SY	Shrewsbury	England
TA	Taunton	England
CW	Crewe	England
BD	Bradford	England
M	Manchester	England
CT	Canterbury	England
ML	Motherwell	Scotland
TS	Teesside	England
DA	Dartford	England
SL	Slough	England
DL	Darlington	England
UB	Uxbridge	England
ZE	Zetland	Scotland
FY	Fylde	England
RG	Reading	England
\.


--
-- Name: pk_postcodearea; Type: CONSTRAINT; Schema: public; Owner: twain; Tablespace: 
--

ALTER TABLE ONLY gb_postcodearea
    ADD CONSTRAINT pk_postcodearea PRIMARY KEY (postcodeareaid);

--
-- PostgreSQL database dump complete
--

